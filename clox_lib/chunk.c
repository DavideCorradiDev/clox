#include <stdlib.h>
#include "chunk.h"
#include "memory.h"
#include "vm.h"

void init_line_start_array(Vm *vm, LineStartArray *array)
{
    array->capacity = 0;
    array->count = 0;
    array->values = NULL;
}

void free_line_start_array(Vm *vm, LineStartArray *array)
{
    FREE_ARRAY(Value, array->values, array->capacity);
    init_line_start_array(vm, array);
}

void write_line_start_array(Vm *vm, LineStartArray *array, int offset, int line)
{
    if (array->count > 0 && array->values[array->count - 1].line == line)
    {
        return;
    }

    if (array->capacity < array->count + 1)
    {
        int old_capacity = array->capacity;
        array->capacity = GROW_CAPACITY(old_capacity);
        array->values = GROW_ARRAY(LineStart, array->values, old_capacity, array->capacity);
    }
    array->values[array->count].offset = offset;
    array->values[array->count].line = line;
    array->count++;
}

void init_chunk(Vm *vm, Chunk *chunk)
{
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    init_line_start_array(vm, &chunk->lines);
    init_value_array(vm, &chunk->constants);
}

void free_chunk(Vm *vm, Chunk *chunk)
{
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    free_line_start_array(vm, &chunk->lines);
    free_value_array(vm, &chunk->constants);
    init_chunk(vm, chunk);
}

void write_chunk(Vm *vm, Chunk *chunk, uint8_t byte, int line)
{
    if (chunk->capacity < chunk->count + 1)
    {
        int old_capacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(old_capacity);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code, old_capacity, chunk->capacity);
    }
    chunk->code[chunk->count] = byte;
    write_line_start_array(vm, &chunk->lines, chunk->count, line);
    chunk->count++;
}

void write_constant(Vm *vm, Chunk *chunk, Value value, int line)
{
    int index = add_constant(vm, chunk, value);
    if (index < 256)
    {
        write_chunk(vm, chunk, OP_CONSTANT, line);
        write_chunk(vm, chunk, (uint8_t)index, line);
    }
    else
    {
        write_chunk(vm, chunk, OP_CONSTANT_LONG, line);
        write_chunk(vm, chunk, (uint8_t)(index & 0xff), line);
        write_chunk(vm, chunk, (uint8_t)((index >> 8) & 0xff), line);
        write_chunk(vm, chunk, (uint8_t)((index >> 16) & 0xff), line);
    }
}

int add_constant(Vm *vm, Chunk *chunk, Value value)
{
    push(vm, value);
    write_value_array(vm, &chunk->constants, value);
    pop(vm);
    return chunk->constants.count - 1;
}

int get_line(Chunk *chunk, int offset)
{
    int l = 0;
    int r = chunk->lines.count - 1;
    while (true)
    {
        int m = (l + r) / 2;
        if (offset < chunk->lines.values[m].offset)
        {
            r = m - 1;
        }
        else if (m == chunk->lines.count - 1 || offset < chunk->lines.values[m + 1].offset)
        {
            return chunk->lines.values[m].line;
        }
        else
        {
            l = m + 1;
        }
    }
    return -1;
}
