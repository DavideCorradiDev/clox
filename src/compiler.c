#include <stdio.h>
#include <stdlib.h>
#include "compiler.h"
#include "common.h"
#include "object.h"
#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef void (*ParseFn)(Compiler *);

typedef struct
{
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

static void expression(Compiler *compiler);
static ParseRule *get_rule(TokenType type);
static void parse_precedence(Compiler *compiler, Precedence precedence);

static void error_at(Compiler *compiler, Token *token, const char *message)
{
    if (compiler->parser.panic_mode)
    {
        return;
    }
    compiler->parser.panic_mode = true;

    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF)
    {
        fprintf(stderr, " at end");
    }
    else if (token->type == TOKEN_ERROR)
    {
        // Nothing to do
    }
    else
    {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }
    fprintf(stderr, ": %s\n", message);
    compiler->parser.had_error = true;
}

static void error_at_current(Compiler *compiler, const char *message)
{
    error_at(compiler, &(compiler->parser.current), message);
}

static void error(Compiler *compiler, const char *message)
{
    error_at(compiler, &(compiler->parser.previous), message);
}

static void advance(Compiler *compiler)
{
    compiler->parser.previous = compiler->parser.current;
    for (;;)
    {
        compiler->parser.current = scan_token(&compiler->scanner);
        if (compiler->parser.current.type != TOKEN_ERROR)
        {
            break;
        }
        error_at_current(compiler, compiler->parser.current.start);
    }
}

static void consume(Compiler *compiler, TokenType type, const char *message)
{
    if (compiler->parser.current.type == type)
    {
        advance(compiler);
        return;
    }
    error_at(compiler, &(compiler->parser.current), message);
}

static void emit_byte(Compiler *compiler, uint8_t byte)
{
    write_chunk(compiler->chunk, byte, compiler->parser.previous.line);
}

static void emit_bytes(Compiler *compiler, uint8_t byte1, uint8_t byte2)
{
    emit_byte(compiler, byte1);
    emit_byte(compiler, byte2);
}

static void emit_return(Compiler *compiler)
{
    emit_byte(compiler, OP_RETURN);
}

static void emit_constant(Compiler *compiler, Value value)
{
    write_constant(compiler->chunk, value, compiler->parser.previous.line);
}

static void end_compiler(Compiler *compiler)
{
    emit_return(compiler);
#ifdef DEBUG_PRINT_CODE
    if (!compiler->parser.had_error)
    {
        disassemble_chunk(compiler->chunk, "code");
    }
#endif
}

static void parse_precedence(Compiler *compiler, Precedence precedence)
{
    advance(compiler);
    ParseFn prefix_rule = get_rule(compiler->parser.previous.type)->prefix;
    if (prefix_rule == NULL)
    {
        error(compiler, "Expect expression.");
        return;
    }
    prefix_rule(compiler);

    while (precedence <= get_rule(compiler->parser.current.type)->precedence)
    {
        advance(compiler);
        ParseFn infix_rule = get_rule(compiler->parser.previous.type)->infix;
        infix_rule(compiler);
    }
}

static void expression(Compiler *compiler)
{
    parse_precedence(compiler, PREC_ASSIGNMENT);
}

static void grouping(Compiler *compiler)
{
    expression(compiler);
    consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(Compiler *compiler)
{
    double value = strtod(compiler->parser.previous.start, NULL);
    emit_constant(compiler, NUMBER_VAL(value));
}

static void string(Compiler *compiler)
{
    emit_constant(compiler, OBJ_VAL(copy_string(compiler->vm, compiler->parser.previous.start + 1, compiler->parser.previous.length - 2)));
}

static void unary(Compiler *compiler)
{
    TokenType operator_type = compiler->parser.previous.type;
    parse_precedence(compiler, PREC_UNARY);
    switch (operator_type)
    {
    case TOKEN_MINUS:
        emit_byte(compiler, OP_NEGATE);
        break;
    case TOKEN_BANG:
        emit_byte(compiler, OP_NOT);
        break;
    default:
        break;
    }
}

static void binary(Compiler *compiler)
{
    TokenType operator_type = compiler->parser.previous.type;
    ParseRule *rule = get_rule(operator_type);
    parse_precedence(compiler, (Precedence)(rule->precedence + 1));
    switch (operator_type)
    {
    case TOKEN_PLUS:
        emit_byte(compiler, OP_ADD);
        break;
    case TOKEN_MINUS:
        emit_byte(compiler, OP_SUBTRACT);
        break;
    case TOKEN_STAR:
        emit_byte(compiler, OP_MULTIPLY);
        break;
    case TOKEN_SLASH:
        emit_byte(compiler, OP_DIVIDE);
        break;
    case TOKEN_BANG_EQUAL:
        emit_bytes(compiler, OP_EQUAL, OP_NOT);
        break;
    case TOKEN_EQUAL_EQUAL:
        emit_byte(compiler, OP_EQUAL);
        break;
    case TOKEN_GREATER:
        emit_byte(compiler, OP_GREATER);
        break;
    case TOKEN_GREATER_EQUAL:
        emit_bytes(compiler, OP_LESS, OP_NOT);
        break;
    case TOKEN_LESS:
        emit_byte(compiler, OP_LESS);
        break;
    case TOKEN_LESS_EQUAL:
        emit_bytes(compiler, OP_GREATER, OP_NOT);
        break;
    default:
        break;
    }
}

static void literal(Compiler *compiler)
{
    switch (compiler->parser.previous.type)
    {
    case TOKEN_FALSE:
        emit_byte(compiler, OP_FALSE);
        break;
    case TOKEN_TRUE:
        emit_byte(compiler, OP_TRUE);
        break;
    case TOKEN_NIL:
        emit_byte(compiler, OP_NIL);
        break;
    default:
        return; // Unreachable.
    }
}

static ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, NULL, PREC_NONE},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, NULL, PREC_NONE},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER] = {NULL, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, NULL, PREC_NONE},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, NULL, PREC_NONE},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {NULL, NULL, PREC_NONE},
    [TOKEN_THIS] = {NULL, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE}};

static ParseRule *get_rule(TokenType type)
{
    return &rules[type];
}

void init_parser(Parser *parser)
{
    parser->had_error = false;
    parser->panic_mode = false;
}

void free_parser(Parser *parser)
{
    // Nothing to do.
}

void init_compiler(Compiler *compiler, Vm *vm, Chunk *chunk, const char *source)
{
    init_scanner(&compiler->scanner, source);
    init_parser(&compiler->parser);
    compiler->vm = vm;
    compiler->chunk = chunk;
}

void free_compiler(Compiler *compiler)
{
    free_scanner(&(compiler->scanner));
    free_parser(&(compiler->parser));
}

bool compile(Compiler *compiler)
{
    advance(compiler);
    expression(compiler);
    consume(compiler, TOKEN_EOF, "Expect end of expression.");
    end_compiler(compiler);

    return !compiler->parser.had_error;
}
