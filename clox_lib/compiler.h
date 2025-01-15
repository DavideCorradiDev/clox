#ifndef CLOX_COMPILER_H
#define CLOX_COMPILER_H

#include "common.h"
#include "chunk.h"
#include "object.h"
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
    Token name;
    int depth;
    bool is_captured;
} Local;

typedef struct
{
    uint8_t index;
    bool is_local;
} Upvalue;

typedef enum
{
    TYPE_FUNCTION,
    TYPE_INITIALIZER,
    TYPE_METHOD,
    TYPE_SCRIPT,
} FunctionType;

typedef struct Compiler
{
    struct Compiler *enclosing;
    struct ClassCompiler *current_class;
    Scanner *scanner;
    Parser *parser;
    Vm *vm;
    ObjFunction *function;
    FunctionType type;
    Local locals[UINT8_COUNT];
    int local_count;
    Upvalue upvalues[UINT8_COUNT];
    int scope_depth;
} Compiler;

typedef struct ClassCompiler
{
    struct ClassCompiler *enclosing;
    bool has_superclass;
} ClassCompiler;

void init_compiler(Compiler *compiler, Scanner *scanner, Parser *parser, Vm *vm, FunctionType type);
void free_compiler(Compiler *compiler);
Chunk *current_chunk(Compiler *compiler);
ObjFunction *compile(Compiler *compiler);
void mark_compiler_roots(Compiler *compiler);

#endif
