#ifndef CLOX_COMPILER_H
#define CLOX_COMPILER_H

#include "common.h"
#include "chunk.h"
#include "scanner.h"
#include "vm.h"

typedef struct
{
    Token current;
    Token previous;
    bool had_error;
    bool panic_mode;
} Parser;

void init_parser(Parser *parser);
void free_parser(Parser *parser);

typedef enum
{
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_OR,         // or
    PREC_AND,        // and
    PREC_EQUALITY,   // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_TERM,       // + -
    PREC_FACTOR,     // * /
    PREC_UNARY,      // ! -
    PREC_CALL,       // . ()
    PREC_PRIMARY,
} Precedence;

typedef struct
{
    Scanner scanner;
    Parser parser;
    Vm *vm;
    Chunk *chunk;
} Compiler;

void init_compiler(Compiler *compiler, Vm *vm, Chunk *chunk, const char *source);
void free_compiler(Compiler *compiler);
bool compile(Compiler *compiler);

#endif
