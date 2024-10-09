#include <stdio.h>
#include "compiler.h"
#include "vm.h"
#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif

static void push(Vm *vm, Value value)
{
    *vm->stack_top = value;
    vm->stack_top++;
}

static Value pop(Vm *vm)
{
    vm->stack_top--;
    return *vm->stack_top;
}

static Value *top(Vm *vm)
{
    return vm->stack_top - 1;
}

static void reset_stack(Vm *vm)
{
    vm->stack_top = (Value *)&vm->stack;
}

static InterpretResult run(Vm *vm)
{
#define READ_BYTE() (*vm->ip++)
#define READ_CONSTANT() (vm->chunk->constants.values[READ_BYTE()])
#define BINARY_OP(op)             \
    do                            \
    {                             \
        double b = pop(vm);       \
        *top(vm) = *top(vm) op b; \
    } while (false)

    for (;;)
    {
#ifdef DEBUG_TRACE_EXECUTION
        printf("Stack   | ");
        for (Value *slot = vm->stack; slot < vm->stack_top; slot++)
        {
            printf("[ ");
            print_value(*slot);
            printf(" ]");
        }
        printf("\n");
        disassemble_instruction(vm->chunk, (int)(vm->ip - vm->chunk->code));
#endif
        uint8_t instruction;
        switch (instruction = READ_BYTE())
        {
        case OP_CONSTANT:
        {
            Value constant = READ_CONSTANT();
            push(vm, constant);
            break;
        }
        case OP_CONSTANT_LONG:
        {
            int index = READ_BYTE() | (READ_BYTE() << 8) | (READ_BYTE() << 16);
            int constant = vm->chunk->constants.values[index];
            push(vm, constant);
            break;
        }
        case OP_NEGATE:
            *top(vm) = -*top(vm);
            break;
        case OP_ADD:
            BINARY_OP(+);
            break;
        case OP_SUBTRACT:
            BINARY_OP(-);
            break;
        case OP_MULTIPLY:
            BINARY_OP(*);
            break;
        case OP_DIVIDE:
            BINARY_OP(/);
            break;
        case OP_RETURN:
            print_value(pop(vm));
            printf("\n");
            return INTERPRET_OK;
        }
    }

#undef BINARY_OP
#undef READ_CONSTANT
#undef READ_BYTE
}

void init_vm(Vm *vm)
{
    reset_stack(vm);
}

void free_vm(Vm *vm)
{
}

InterpretResult interpret(Vm *vm, const char *source)
{
    Chunk chunk;
    init_chunk(&chunk);

    if (!compile(&chunk, source))
    {
        free_chunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    vm->chunk = &chunk;
    vm->ip = vm->chunk->code;

    InterpretResult result = run(vm);

    free_chunk(&chunk);
    return result;
}
