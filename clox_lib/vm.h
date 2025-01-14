#ifndef CLOX_VM_H
#define CLOX_VM_H

#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct Compiler Compiler;

typedef struct CallFrame
{
    ObjClosure *closure;
    uint8_t *ip;
    Value *slots;
} CallFrame;

typedef struct Vm
{
    Compiler *compiler;
    CallFrame frames[FRAMES_MAX];
    int frame_count;
    Value stack[STACK_MAX];
    Value *stack_top;
    Table globals;
    Table strings;
    ObjString *init_string;
    ObjUpvalue *open_upvalues;
    size_t bytes_allocated;
    size_t next_gc;
    Obj *objects;
    int gray_count;
    int gray_capacity;
    Obj **gray_stack;
} Vm;

typedef enum
{
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void init_vm(Vm *vm);
void free_vm(Vm *vm);
void push(Vm *vm, Value value);
Value pop(Vm *vm);
InterpretResult interpret(Vm *vm, const char *source);

#endif
