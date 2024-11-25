#include "vm.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "compiler.h"
#include "memory.h"
#include "object.h"
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

// static Value *top(Vm *vm)
// {
//     return vm->stack_top - 1;
// }

static Value peek(Vm *vm, int distance)
{
    return vm->stack_top[-1 - distance];
}

static bool is_falsey(Value value)
{
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(Vm *vm)
{
    ObjString *b = AS_STRING(pop(vm));
    ObjString *a = AS_STRING(pop(vm));
    int length = a->length + b->length;
    char *chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString *result = take_string(vm, chars, length);
    push(vm, OBJ_VAL(result));
}

static void reset_stack(Vm *vm)
{
    vm->stack_top = (Value *)&vm->stack;
}

static void runtime_error(Vm *vm, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    size_t instruction = vm->ip - vm->chunk->code - 1;
    int line = vm->chunk->lines.values[instruction].line;
    fprintf(stderr, "[line %d] in script\n", line);
    reset_stack(vm);
}

static InterpretResult run(Vm *vm)
{
#define READ_BYTE() (*vm->ip++)
#define READ_SHORT() (vm->ip += 2, (uint16_t)((vm->ip[-2] << 8) | vm->ip[-1]))
#define READ_3_BYTES() (READ_BYTE() | (READ_BYTE() << 8) | (READ_BYTE() << 16))
#define READ_CONSTANT() (vm->chunk->constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG() (vm->chunk->constants.values[READ_3_BYTES()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(value_type, op)                               \
    do                                                          \
    {                                                           \
        if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) \
        {                                                       \
            runtime_error(vm, "Operands must be numbers.");     \
            return INTERPRET_RUNTIME_ERROR;                     \
        }                                                       \
        double b = AS_NUMBER(pop(vm));                          \
        double a = AS_NUMBER(pop(vm));                          \
        push(vm, value_type(a op b));                           \
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
            Value constant = READ_CONSTANT_LONG();
            push(vm, constant);
            break;
        }
        case OP_NIL:
            push(vm, NIL_VAL);
            break;
        case OP_TRUE:
            push(vm, BOOL_VAL(true));
            break;
        case OP_FALSE:
            push(vm, BOOL_VAL(false));
            break;
        case OP_GET_LOCAL:
        {
            uint8_t slot = READ_BYTE();
            push(vm, vm->stack[slot]);
            break;
        }
        case OP_SET_LOCAL:
        {
            uint8_t slot = READ_BYTE();
            vm->stack[slot] = peek(vm, 0);
            break;
        }
        case OP_GET_GLOBAL:
        {
            ObjString *name = READ_STRING();
            Value value;
            if (!table_get(&vm->globals, name, &value))
            {
                runtime_error(vm, "Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            push(vm, value);
            break;
        }
        case OP_DEFINE_GLOBAL:
        {
            ObjString *name = READ_STRING();
            table_set(&vm->globals, name, peek(vm, 0));
            pop(vm);
            break;
        }
        case OP_SET_GLOBAL:
        {
            ObjString *name = READ_STRING();
            if (table_set(&vm->globals, name, peek(vm, 0)))
            {
                table_delete(&vm->globals, name);
                runtime_error(vm, "Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_POP:
            pop(vm);
            break;
        case OP_EQUAL:
        {
            Value b = pop(vm);
            Value a = pop(vm);
            push(vm, BOOL_VAL(values_equal(a, b)));
            break;
        }
        case OP_GREATER:
            BINARY_OP(BOOL_VAL, >);
            break;
        case OP_LESS:
            BINARY_OP(BOOL_VAL, <);
            break;
        case OP_ADD:
            if (IS_STRING(peek(vm, 0)) && IS_STRING(peek(vm, 1)))
            {
                concatenate(vm);
            }
            else if (IS_NUMBER(peek(vm, 0)) && IS_NUMBER(peek(vm, 1)))
            {
                double b = AS_NUMBER(pop(vm));
                double a = AS_NUMBER(pop(vm));
                push(vm, NUMBER_VAL(a + b));
            }
            else
            {
                runtime_error(vm, "Operands must be two numbers or two strings.");
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        case OP_SUBTRACT:
            BINARY_OP(NUMBER_VAL, -);
            break;
        case OP_MULTIPLY:
            BINARY_OP(NUMBER_VAL, *);
            break;
        case OP_DIVIDE:
            BINARY_OP(NUMBER_VAL, /);
            break;
        case OP_NOT:
            push(vm, BOOL_VAL(is_falsey(pop(vm))));
            break;
        case OP_NEGATE:
            if (!IS_NUMBER(peek(vm, 0)))
            {
                runtime_error(vm, "Operand must be a number.");
                return INTERPRET_RUNTIME_ERROR;
            }
            push(vm, NUMBER_VAL(-AS_NUMBER(pop(vm))));
            break;
        case OP_PRINT:
            print_value(pop(vm));
            printf("\n");
            break;
        case OP_JUMP:
        {
            uint16_t offset = READ_SHORT();
            vm->ip += offset;
            break;
        }
        case OP_JUMP_IF_FALSE:
        {
            uint16_t offset = READ_SHORT();
            if (is_falsey(peek(vm, 0)))
            {
                vm->ip += offset;
            }
            break;
        }
        case OP_LOOP:
        {
            uint16_t offset = READ_SHORT();
            vm->ip -= offset;
            break;
        }
        case OP_RETURN:
            return INTERPRET_OK;
        }
    }

#undef BINARY_OP
#undef READ_STRING
#undef READ_CONSTANT_LONG
#undef READ_CONSTANT
#undef READ_3_BYTES
#undef READ_SHORT
#undef READ_BYTE
}

void init_vm(Vm *vm)
{
    reset_stack(vm);
    init_table(&vm->globals);
    init_table(&vm->strings);
    vm->objects = NULL;
}

void free_vm(Vm *vm)
{
    free_objects(vm);
    free_table(&vm->strings);
    free_table(&vm->globals);
}

InterpretResult interpret(Vm *vm, const char *source)
{
    Chunk chunk;
    init_chunk(&chunk);

    Compiler compiler;
    init_compiler(&compiler, vm, &chunk, source);

    if (!compile(&compiler))
    {
        free_compiler(&compiler);
        free_chunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    vm->chunk = &chunk;
    vm->ip = vm->chunk->code;

    InterpretResult result = run(vm);

    free_compiler(&compiler);
    free_chunk(&chunk);
    return result;
}
