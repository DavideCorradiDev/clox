#ifndef CLOX_VM_H
#define CLOX_VM_H

#include "chunk.h"
#include "value.h"

#define STACK_MAX 256

typedef struct
{
    Chunk *chunk;
    uint8_t *ip;
    Value stack[STACK_MAX];
    Value *stack_top;
    Obj *objects;
} Vm;

typedef enum
{
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void init_vm(Vm *vm);
void free_vm(Vm *vm);
InterpretResult interpret(Vm *vm, const char *source);

#endif
