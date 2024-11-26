#ifndef CLOX_VM_H
#define CLOX_VM_H

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct
{
    ObjFunction *function;
    uint8_t *ip;
    Value *slots;
} CallFrame;

typedef struct
{
    CallFrame frames[FRAMES_MAX];
    int frame_count;
    Value stack[STACK_MAX];
    Value *stack_top;
    Table globals;
    Table strings;
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

ObjFunction *new_function(Vm *vm);
ObjNative *new_native(Vm *vm, NativeFn function);
ObjString *take_string(Vm *vm, char *chars, int length);
ObjString *copy_string(Vm *vm, const char *chars, int length);

#endif
