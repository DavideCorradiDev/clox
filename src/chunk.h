#ifndef CLOX_CHUNK_H
#define CLOX_CHUNK_H

#include "common.h"
#include "value.h"

typedef enum
{
    OP_CONSTANT,
    OP_CONSTANT_LONG,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NOT,
    OP_NEGATE,
    OP_RETURN,
} OpCode;

typedef struct
{
    int offset;
    int line;
} LineStart;

typedef struct
{
    int capacity;
    int count;
    LineStart *values;
} LineStartArray;

void init_line_start_array(LineStartArray *array);
void free_line_start_array(LineStartArray *array);
void write_line_start_array(LineStartArray *array, int offset, int line);

typedef struct
{
    int count;
    int capacity;
    uint8_t *code;
    LineStartArray lines;
    ValueArray constants;
} Chunk;

void init_chunk(Chunk *chunk);
void free_chunk(Chunk *chunk);
void write_chunk(Chunk *chunk, uint8_t byte, int line);
void write_constant(Chunk *chunk, Value value, int line);
int add_constant(Chunk *chunk, Value value);
int get_line(Chunk *chunk, int offset);

#endif
