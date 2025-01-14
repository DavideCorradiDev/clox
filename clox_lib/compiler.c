#include <stdio.h>
#include <stdlib.h>
#include "compiler.h"
#include "common.h"
#include "memory.h"
#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

#include <string.h>

typedef void (*ParseFn)(Compiler *compiler, bool can_assign);

typedef struct
{
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

static void expression(Compiler *compiler);
static void declaration(Compiler *compiler);
static void named_variable(Compiler *compiler, Token name, bool can_assign);
static void statement(Compiler *compiler);
static ParseRule *get_rule(TokenType type);
static void parse_precedence(Compiler *compiler, Precedence precedence);

static void error_at(Compiler *compiler, Token *token, const char *message)
{
    if (compiler->parser->panic_mode)
    {
        return;
    }
    compiler->parser->panic_mode = true;

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
    compiler->parser->had_error = true;
}

static void error_at_current(Compiler *compiler, const char *message)
{
    error_at(compiler, &(compiler->parser->current), message);
}

static void error(Compiler *compiler, const char *message)
{
    error_at(compiler, &(compiler->parser->previous), message);
}

static void advance(Compiler *compiler)
{
    compiler->parser->previous = compiler->parser->current;
    for (;;)
    {
        compiler->parser->current = scan_token(compiler->scanner);
        if (compiler->parser->current.type != TOKEN_ERROR)
        {
            break;
        }
        error_at_current(compiler, compiler->parser->current.start);
    }
}

static void consume(Compiler *compiler, TokenType type, const char *message)
{
    if (compiler->parser->current.type == type)
    {
        advance(compiler);
        return;
    }
    error_at(compiler, &(compiler->parser->current), message);
}

static bool check(Compiler *compiler, TokenType type)
{
    return compiler->parser->current.type == type;
}

static bool match(Compiler *compiler, TokenType type)
{
    if (!check(compiler, type))
    {
        return false;
    }
    advance(compiler);
    return true;
}

static void emit_byte(Compiler *compiler, uint8_t byte)
{
    write_chunk(compiler->vm, current_chunk(compiler), byte, compiler->parser->previous.line);
}

static void emit_bytes(Compiler *compiler, uint8_t byte1, uint8_t byte2)
{
    emit_byte(compiler, byte1);
    emit_byte(compiler, byte2);
}

static void emit_loop(Compiler *compiler, int loop_start)
{
    emit_byte(compiler, OP_LOOP);

    int offset = current_chunk(compiler)->count - loop_start + 2;
    if (offset > UINT16_MAX)
    {
        error(compiler, "Loop body too large.");
    }

    emit_byte(compiler, (offset >> 8) & 0xff);
    emit_byte(compiler, offset & 0xff);
}

static int emit_jump(Compiler *compiler, uint8_t instruction)
{
    emit_byte(compiler, instruction);
    emit_byte(compiler, 0xff);
    emit_byte(compiler, 0xff);
    return current_chunk(compiler)->count - 2;
}

static void emit_return(Compiler *compiler)
{
    if (compiler->type == TYPE_INITIALIZER)
    {
        emit_bytes(compiler, OP_GET_LOCAL, 0);
    }
    else
    {
        emit_byte(compiler, OP_NIL);
    }
    emit_byte(compiler, OP_RETURN);
}

static void emit_constant(Compiler *compiler, Value value)
{
    write_constant(compiler->vm, current_chunk(compiler), value, compiler->parser->previous.line);
}

static void patch_jump(Compiler *compiler, int offset)
{
    int jump = current_chunk(compiler)->count - offset - 2;

    if (jump > UINT16_MAX)
    {
        error(compiler, "Too much code to jump over.");
    }

    current_chunk(compiler)->code[offset] = (jump >> 8) & 0xff;
    current_chunk(compiler)->code[offset + 1] = jump & 0xff;
}

static ObjFunction *end_compiler(Compiler *compiler)
{
    emit_return(compiler);
    ObjFunction *function = compiler->function;
#ifdef DEBUG_PRINT_CODE
    if (!compiler->parser->had_error)
    {
        disassemble_chunk(current_chunk(compiler), function->name != NULL ? function->name->chars : "<script>");
    }
#endif
    return function;
}

static void begin_scope(Compiler *compiler)
{
    compiler->scope_depth++;
}

static void end_scope(Compiler *compiler)
{
    compiler->scope_depth--;

    while (compiler->local_count > 0 && compiler->locals[compiler->local_count - 1].depth > compiler->scope_depth)
    {
        if (compiler->locals[compiler->local_count - 1].is_captured)
        {
            emit_byte(compiler, OP_CLOSE_UPVALUE);
        }
        else
        {
            emit_byte(compiler, OP_POP);
        }
        compiler->local_count--;
    }
}

static void parse_precedence(Compiler *compiler, Precedence precedence)
{
    advance(compiler);
    ParseFn prefix_rule = get_rule(compiler->parser->previous.type)->prefix;
    if (prefix_rule == NULL)
    {
        error(compiler, "Expect expression.");
        return;
    }
    bool can_assign = precedence <= PREC_ASSIGNMENT;
    prefix_rule(compiler, can_assign);

    while (precedence <= get_rule(compiler->parser->current.type)->precedence)
    {
        advance(compiler);
        ParseFn infix_rule = get_rule(compiler->parser->previous.type)->infix;
        infix_rule(compiler, can_assign);
    }

    if (can_assign && match(compiler, TOKEN_EQUAL))
    {
        error(compiler, "Invalid assignment target.");
    }
}

static uint8_t identifier_constant(Compiler *compiler, Token *name)
{
    return add_constant(compiler->vm, current_chunk(compiler), OBJ_VAL(copy_string(compiler->vm, name->start, name->length)));
}

static bool identifiers_equal(Token *a, Token *b)
{
    if (a->length != b->length)
    {
        return false;
    }
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolve_local(Compiler *compiler, Token *name)
{
    for (int i = compiler->local_count - 1; i >= 0; i--)
    {
        Local *local = &compiler->locals[i];
        if (identifiers_equal(name, &local->name))
        {
            if (local->depth == -1)
            {
                error(compiler, "Can't read local variable in its own initializer.");
            }
            return i;
        }
    }
    return -1;
}

static int add_upvalue(Compiler *compiler, uint8_t index, bool is_local)
{
    int upvalue_count = compiler->function->upvalue_count;

    for (int i = 0; i < upvalue_count; i++)
    {
        Upvalue *upvalue = &compiler->upvalues[i];
        if ((upvalue->index == index) && (upvalue->is_local == is_local))
        {
            return i;
        }
    }

    if (upvalue_count == UINT8_COUNT)
    {
        error(compiler, "Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalue_count].is_local = is_local;
    compiler->upvalues[upvalue_count].index = index;
    return compiler->function->upvalue_count++;
}

static int resolve_upvalue(Compiler *compiler, Token *name)
{
    if (compiler->enclosing == NULL)
    {
        return -1;
    }

    int local = resolve_local(compiler->enclosing, name);
    if (local != -1)
    {
        compiler->enclosing->locals[local].is_captured = true;
        return add_upvalue(compiler, (uint8_t)local, true);
    }

    int upvalue = resolve_upvalue(compiler->enclosing, name);
    if (upvalue != -1)
    {
        return add_upvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

static void add_local(Compiler *compiler, Token *name)
{
    if (compiler->local_count == UINT8_COUNT)
    {
        error(compiler, "Too many local variables in function.");
        return;
    }

    Local *local = &compiler->locals[compiler->local_count++];
    local->name = *name;
    local->depth = -1;
    local->is_captured = false;
}

static void declare_variable(Compiler *compiler)
{
    if (compiler->scope_depth == 0)
    {
        return;
    }
    Token *name = &compiler->parser->previous;
    for (int i = compiler->local_count - 1; i >= 0; i--)
    {
        Local *local = &compiler->locals[i];
        if (local->depth != -1 && local->depth < compiler->scope_depth)
        {
            break;
        }
        if (identifiers_equal(name, &local->name))
        {
            error(compiler, "Already a variable with this name in this scope.");
        }
    }
    add_local(compiler, name);
}

static uint8_t parse_variable(Compiler *compiler, const char *error_message)
{
    consume(compiler, TOKEN_IDENTIFIER, error_message);
    declare_variable(compiler);
    if (compiler->scope_depth > 0)
    {
        return 0;
    }
    return identifier_constant(compiler, &compiler->parser->previous);
}

static void mark_initialized(Compiler *compiler)
{
    if (compiler->scope_depth == 0)
    {
        return;
    }
    compiler->locals[compiler->local_count - 1].depth = compiler->scope_depth;
}

static void define_variable(Compiler *compiler, uint8_t global)
{
    if (compiler->scope_depth > 0)
    {
        mark_initialized(compiler);
        return;
    }
    emit_bytes(compiler, OP_DEFINE_GLOBAL, global);
}

static uint8_t argument_list(Compiler *compiler)
{
    uint8_t arg_count = 0;
    if (!check(compiler, TOKEN_RIGHT_PAREN))
    {
        do
        {
            expression(compiler);
            if (arg_count == 255)
            {
                error(compiler, "Can't have more than 255 arguments.");
            }
            arg_count++;
        } while (match(compiler, TOKEN_COMMA));
    }
    consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return arg_count;
}

static void and_(Compiler *compiler, bool can_assign)
{
    int end_jump = emit_jump(compiler, OP_JUMP_IF_FALSE);
    emit_byte(compiler, OP_POP);
    parse_precedence(compiler, PREC_AND);
    patch_jump(compiler, end_jump);
}

static void or_(Compiler *compiler, bool can_assign)
{
    int else_jump = emit_jump(compiler, OP_JUMP_IF_FALSE);
    int end_jump = emit_jump(compiler, OP_JUMP);
    patch_jump(compiler, else_jump);
    emit_byte(compiler, OP_POP);
    parse_precedence(compiler, PREC_OR);
    patch_jump(compiler, end_jump);
}

static void expression(Compiler *compiler)
{
    parse_precedence(compiler, PREC_ASSIGNMENT);
}

static void block(Compiler *compiler)
{
    while (!check(compiler, TOKEN_RIGHT_BRACE) && !check(compiler, TOKEN_EOF))
    {
        declaration(compiler);
    }
    consume(compiler, TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(Compiler *compiler, FunctionType type)
{
    Compiler sub_compiler;
    init_compiler(&sub_compiler, compiler->scanner, compiler->parser, compiler->vm, type);
    compiler->vm->compiler = &sub_compiler;
    sub_compiler.enclosing = compiler;
    sub_compiler.current_class = compiler->current_class;

    begin_scope(&sub_compiler);

    consume(&sub_compiler, TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(&sub_compiler, TOKEN_RIGHT_PAREN))
    {
        do
        {
            sub_compiler.function->arity++;
            if (sub_compiler.function->arity > 255)
            {
                error_at_current(&sub_compiler, "Can't have more than 255 parameters.");
            }
            uint8_t constant = parse_variable(&sub_compiler, "Expect parameter name.");
            define_variable(&sub_compiler, constant);
        } while (match(&sub_compiler, TOKEN_COMMA));
    }
    consume(&sub_compiler, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(&sub_compiler, TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block(&sub_compiler);

    ObjFunction *function = end_compiler(&sub_compiler);
    compiler->vm->compiler = compiler;
    free_compiler(&sub_compiler);

    emit_bytes(compiler, OP_CLOSURE, add_constant(compiler->vm, current_chunk(compiler), OBJ_VAL(function)));
    for (int i = 0; i < function->upvalue_count; i++)
    {
        emit_byte(compiler, sub_compiler.upvalues[i].is_local ? 1 : 0);
        emit_byte(compiler, sub_compiler.upvalues[i].index);
    }
}

static void method(Compiler *compiler)
{
    consume(compiler, TOKEN_IDENTIFIER, "Expect method name.");
    uint8_t constant = identifier_constant(compiler, &compiler->parser->previous);

    FunctionType type = TYPE_METHOD;
    if (compiler->parser->previous.length == 4 && memcmp(compiler->parser->previous.start, "init", 4) == 0)
    {
        type = TYPE_INITIALIZER;
    }
    function(compiler, type);

    emit_bytes(compiler, OP_METHOD, constant);
}

static void class_declaration(Compiler *compiler)
{
    consume(compiler, TOKEN_IDENTIFIER, "Expect class name.");
    Token class_name = compiler->parser->previous;
    uint8_t name_constant = identifier_constant(compiler, &compiler->parser->previous);
    declare_variable(compiler);

    emit_bytes(compiler, OP_CLASS, name_constant);
    define_variable(compiler, name_constant);

    ClassCompiler class_compiler;
    class_compiler.enclosing = compiler->current_class;
    compiler->current_class = &class_compiler;

    named_variable(compiler, class_name, false);
    consume(compiler, TOKEN_LEFT_BRACE, "Expect '{' before class body.");

    while (!check(compiler, TOKEN_RIGHT_BRACE) && !check(compiler, TOKEN_EOF))
    {
        method(compiler);
    }

    consume(compiler, TOKEN_RIGHT_BRACE, "Expect '}' before class body.");
    emit_byte(compiler, OP_POP);

    compiler->current_class = class_compiler.enclosing;
}

static void fun_declaration(Compiler *compiler)
{
    uint8_t global = parse_variable(compiler, "Expect function name.");
    mark_initialized(compiler);
    function(compiler, TYPE_FUNCTION);
    define_variable(compiler, global);
}

static void var_declaration(Compiler *compiler)
{
    uint8_t global = parse_variable(compiler, "Expect variable name.");

    if (match(compiler, TOKEN_EQUAL))
    {
        expression(compiler);
    }
    else
    {
        emit_byte(compiler, OP_NIL);
    }
    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    define_variable(compiler, global);
}

static void expression_statement(Compiler *compiler)
{
    expression(compiler);
    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after expression.");
    emit_byte(compiler, OP_POP);
}

static void if_statement(Compiler *compiler)
{
    consume(compiler, TOKEN_LEFT_PAREN, "Expect '(' after if.");
    expression(compiler);
    consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int then_jump = emit_jump(compiler, OP_JUMP_IF_FALSE);
    emit_byte(compiler, OP_POP);

    statement(compiler);

    int else_jump = emit_jump(compiler, OP_JUMP);
    patch_jump(compiler, then_jump);
    emit_byte(compiler, OP_POP);

    if (match(compiler, TOKEN_ELSE))
    {
        statement(compiler);
    }

    patch_jump(compiler, else_jump);
}

static void while_statement(Compiler *compiler)
{
    int loop_start = current_chunk(compiler)->count;

    consume(compiler, TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression(compiler);
    consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exit_jump = emit_jump(compiler, OP_JUMP_IF_FALSE);
    emit_byte(compiler, OP_POP);
    statement(compiler);
    emit_loop(compiler, loop_start);

    patch_jump(compiler, exit_jump);
    emit_byte(compiler, OP_POP);
}

static void for_statement(Compiler *compiler)
{
    begin_scope(compiler);

    consume(compiler, TOKEN_LEFT_PAREN, "Expect '(' after for.");
    if (match(compiler, TOKEN_SEMICOLON))
    {
        // No initializer
    }
    else if (match(compiler, TOKEN_VAR))
    {
        var_declaration(compiler);
    }
    else
    {
        expression_statement(compiler);
    }

    int loop_start = current_chunk(compiler)->count;
    int exit_jump = -1;
    if (!match(compiler, TOKEN_SEMICOLON))
    {
        expression(compiler);
        consume(compiler, TOKEN_SEMICOLON, "Expect ';' after loop condition");
        exit_jump = emit_jump(compiler, OP_JUMP_IF_FALSE);
        emit_byte(compiler, OP_POP);
    }

    if (!match(compiler, TOKEN_RIGHT_PAREN))
    {
        int body_jump = emit_jump(compiler, OP_JUMP);
        int increment_start = current_chunk(compiler)->count;
        expression(compiler);
        emit_byte(compiler, OP_POP);
        consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        emit_loop(compiler, loop_start);
        loop_start = increment_start;
        patch_jump(compiler, body_jump);
    }

    statement(compiler);
    emit_loop(compiler, loop_start);

    if (exit_jump != -1)
    {
        patch_jump(compiler, exit_jump);
        emit_byte(compiler, OP_POP);
    }

    end_scope(compiler);
}

static void print_statement(Compiler *compiler)
{
    expression(compiler);
    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after value.");
    emit_byte(compiler, OP_PRINT);
}

static void return_statement(Compiler *compiler)
{
    if (compiler->type == TYPE_SCRIPT)
    {
        error(compiler, "Can't return from top-level code.");
    }

    if (match(compiler, TOKEN_SEMICOLON))
    {
        emit_return(compiler);
    }
    else
    {
        if (compiler->type == TYPE_INITIALIZER)
        {
            error(compiler, "Can't return a value from an initializer.");
        }
        expression(compiler);
        consume(compiler, TOKEN_SEMICOLON, "Expect ';' after return value.");
        emit_byte(compiler, OP_RETURN);
    }
}

static void synchronize(Compiler *compiler)
{
    Parser *parser = compiler->parser;
    parser->panic_mode = false;
    while (parser->current.type != TOKEN_EOF)
    {
        if (parser->previous.type == TOKEN_SEMICOLON)
        {
            return;
        }
        switch (parser->current.type)
        {
        case TOKEN_CLASS:
        case TOKEN_FUN:
        case TOKEN_VAR:
        case TOKEN_FOR:
        case TOKEN_IF:
        case TOKEN_WHILE:
        case TOKEN_PRINT:
        case TOKEN_RETURN:
            return;
        default:; // Do nothing.
        }
        advance(compiler);
    }
}

static void declaration(Compiler *compiler)
{
    if (match(compiler, TOKEN_CLASS))
    {
        class_declaration(compiler);
    }
    if (match(compiler, TOKEN_FUN))
    {
        fun_declaration(compiler);
    }
    else if (match(compiler, TOKEN_VAR))
    {
        var_declaration(compiler);
    }
    else
    {
        statement(compiler);
    }
    if (compiler->parser->panic_mode)
    {
        synchronize(compiler);
    }
}

static void statement(Compiler *compiler)
{
    if (match(compiler, TOKEN_PRINT))
    {
        print_statement(compiler);
    }
    else if (match(compiler, TOKEN_IF))
    {
        if_statement(compiler);
    }
    else if (match(compiler, TOKEN_RETURN))
    {
        return_statement(compiler);
    }
    else if (match(compiler, TOKEN_WHILE))
    {
        while_statement(compiler);
    }
    else if (match(compiler, TOKEN_FOR))
    {
        for_statement(compiler);
    }
    else if (match(compiler, TOKEN_LEFT_BRACE))
    {
        begin_scope(compiler);
        block(compiler);
        end_scope(compiler);
    }
    else
    {
        expression_statement(compiler);
    }
}

static void grouping(Compiler *compiler, bool can_assign)
{
    expression(compiler);
    consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(Compiler *compiler, bool can_assign)
{
    double value = strtod(compiler->parser->previous.start, NULL);
    emit_constant(compiler, NUMBER_VAL(value));
}

static void string(Compiler *compiler, bool can_assign)
{
    emit_constant(compiler, OBJ_VAL(copy_string(compiler->vm, compiler->parser->previous.start + 1, compiler->parser->previous.length - 2)));
}

static void named_variable(Compiler *compiler, Token name, bool can_assign)
{
    uint8_t get_op;
    uint8_t set_op;
    int arg = resolve_local(compiler, &name);
    if (arg != -1)
    {
        get_op = OP_GET_LOCAL;
        set_op = OP_SET_LOCAL;
    }
    else if ((arg = resolve_upvalue(compiler, &name)) != -1)
    {
        get_op = OP_GET_UPVALUE;
        set_op = OP_SET_UPVALUE;
    }
    else
    {
        arg = identifier_constant(compiler, &name);
        get_op = OP_GET_GLOBAL;
        set_op = OP_SET_GLOBAL;
    }
    if (can_assign && match(compiler, TOKEN_EQUAL))
    {
        expression(compiler);
        emit_bytes(compiler, set_op, arg);
    }
    else
    {
        emit_bytes(compiler, get_op, arg);
    }
}

static void variable(Compiler *compiler, bool can_assign)
{
    named_variable(compiler, compiler->parser->previous, can_assign);
}

static void this_(Compiler *compiler, bool can_assign)
{
    if (compiler->current_class == NULL)
    {
        error(compiler, "Can't use 'this' outside of a class.");
        return;
    }
    variable(compiler, false);
}

static void unary(Compiler *compiler, bool can_assign)
{
    TokenType operator_type = compiler->parser->previous.type;
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

static void binary(Compiler *compiler, bool can_assign)
{
    TokenType operator_type = compiler->parser->previous.type;
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

static void call(Compiler *compiler, bool can_assign)
{
    uint8_t arg_count = argument_list(compiler);
    emit_bytes(compiler, OP_CALL, arg_count);
}

static void dot(Compiler *compiler, bool can_assign)
{
    consume(compiler, TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = identifier_constant(compiler, &compiler->parser->previous);

    if (can_assign && match(compiler, TOKEN_EQUAL))
    {
        expression(compiler);
        emit_bytes(compiler, OP_SET_PROPERTY, name);
    }
    else if (match(compiler, TOKEN_LEFT_PAREN))
    {
        uint8_t arg_count = argument_list(compiler);
        emit_bytes(compiler, OP_INVOKE, name);
        emit_byte(compiler, arg_count);
    }
    else
    {
        emit_bytes(compiler, OP_GET_PROPERTY, name);
    }
}

static void literal(Compiler *compiler, bool can_assign)
{
    switch (compiler->parser->previous.type)
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
    [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, dot, PREC_CALL},
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
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, and_, PREC_AND},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, or_, PREC_OR},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {NULL, NULL, PREC_NONE},
    [TOKEN_THIS] = {this_, NULL, PREC_NONE},
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

void init_compiler(Compiler *compiler, Scanner *scanner, Parser *parser, Vm *vm, FunctionType type)
{
    compiler->enclosing = NULL;
    compiler->current_class = NULL;
    compiler->scanner = scanner;
    compiler->parser = parser;
    compiler->vm = vm;
    compiler->type = type;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->function = new_function(vm);

    if (type != TYPE_SCRIPT)
    {
        push(vm, OBJ_VAL(compiler->function));
        compiler->function->name = copy_string(compiler->vm, compiler->parser->previous.start, compiler->parser->previous.length);
        pop(vm);
    }

    Local *local = &compiler->locals[compiler->local_count++];
    local->depth = 0;
    local->is_captured = false;
    if (type != TYPE_FUNCTION)
    {
        local->name.start = "this";
        local->name.length = 4;
    }
    else
    {
        local->name.start = "";
        local->name.length = 0;
    }
}

void free_compiler(Compiler *compiler)
{
}

Chunk *current_chunk(Compiler *compiler)
{
    return &compiler->function->chunk;
}

ObjFunction *compile(Compiler *compiler)
{
    advance(compiler);
    while (!match(compiler, TOKEN_EOF))
    {
        declaration(compiler);
    }
    ObjFunction *function = end_compiler(compiler);

    return compiler->parser->had_error ? NULL : function;
}

void mark_compiler_roots(Compiler *compiler)
{
    Compiler *curr = compiler;
    while (curr != NULL)
    {
        mark_object(curr->vm, (Obj *)curr->function);
        curr = curr->enclosing;
    }
}
