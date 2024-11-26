#include "vm.h"

#include "compiler.h"
#include "object.h"
#include "memory.h"
#include "table.h"
#include "value.h"
#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif

#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static Value clock_native(int arg_count, Value *args)
{
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

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

static Value peek(Vm *vm, int distance)
{
    return vm->stack_top[-1 - distance];
}

static void reset_stack(Vm *vm)
{
    vm->stack_top = (Value *)&vm->stack;
    vm->frame_count = 0;
}

static void runtime_error(Vm *vm, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = vm->frame_count - 1; i >= 0; i--)
    {
        CallFrame *frame = &vm->frames[i];
        ObjFunction *function = frame->function;
        size_t instruction = frame->ip - frame->function->chunk.code - 1;
        int line = get_line(&function->chunk, instruction);
        fprintf(stderr, "[line %d] in ", line);
        if (function->name == NULL)
        {
            fprintf(stderr, "script\n");
        }
        else
        {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    reset_stack(vm);
}

static void define_native(Vm *vm, const char *name, NativeFn function)
{
    push(vm, OBJ_VAL(copy_string(vm, name, (int)strlen(name))));
    push(vm, OBJ_VAL(new_native(vm, function)));
    table_set(&vm->globals, AS_STRING(vm->stack[0]), vm->stack[1]);
    pop(vm);
    pop(vm);
}

static bool call(Vm *vm, ObjFunction *function, int arg_count)
{
    if (arg_count != function->arity)
    {
        runtime_error(vm, "Expected %d arguments but got %d.", function->arity, arg_count);
        return false;
    }

    if (vm->frame_count == FRAMES_MAX)
    {
        runtime_error(vm, "Stack overflow.");
        return false;
    }

    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->function = function;
    frame->ip = function->chunk.code;
    frame->slots = vm->stack_top - arg_count - 1;
    return true;
}

static bool call_value(Vm *vm, Value callee, int arg_count)
{
    if (IS_OBJ(callee))
    {
        switch (OBJ_TYPE(callee))
        {
        case OBJ_FUNCTION:
            return call(vm, AS_FUNCTION(callee), arg_count);
        case OBJ_NATIVE:
        {
            NativeFn native = AS_NATIVE(callee);
            Value result = native(arg_count, vm->stack_top - arg_count);
            vm->stack_top -= arg_count + 1;
            push(vm, result);
            return true;
        }
        default:
            break;
        }
    }
    runtime_error(vm, "Can only call functions and classes.");
    return false;
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

static InterpretResult run(Vm *vm)
{
    CallFrame *frame = &vm->frames[vm->frame_count - 1];
#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, ((uint16_t)(frame->ip[-2]) << 8 | (uint16_t)(frame->ip[-1])))
#define READ_3_BYTES() (frame->ip += 3, ((uint32_t)(frame->ip[-2]) << 16 | (uint32_t)(frame->ip[-2]) << 8 | (uint32_t)(frame->ip[-1])))
#define READ_CONSTANT() (frame->function->chunk.constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG() (frame->function->chunk.constants.values[READ_3_BYTES()])
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

        printf("Globals | ");
        print_table(&vm->globals);
        printf("\n");

        printf("Strings | ");
        print_table(&vm->strings);
        printf("\n");

        disassemble_instruction(&frame->function->chunk, (int)(frame->ip - frame->function->chunk.code));
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
            push(vm, frame->slots[slot]);
            break;
        }
        case OP_SET_LOCAL:
        {
            uint8_t slot = READ_BYTE();
            frame->slots[slot] = peek(vm, 0);
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
            frame->ip += offset;
            break;
        }
        case OP_JUMP_IF_FALSE:
        {
            uint16_t offset = READ_SHORT();
            if (is_falsey(peek(vm, 0)))
            {
                frame->ip += offset;
            }
            break;
        }
        case OP_LOOP:
        {
            uint16_t offset = READ_SHORT();
            frame->ip -= offset;
            break;
        }
        case OP_CALL:
        {
            int arg_count = READ_BYTE();
            if (!call_value(vm, peek(vm, arg_count), arg_count))
            {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm->frames[vm->frame_count - 1];
            break;
        }
        case OP_RETURN:
        {
            Value result = pop(vm);
            vm->frame_count--;
            if (vm->frame_count == 0)
            {
                pop(vm);
                return INTERPRET_OK;
            }

            vm->stack_top = frame->slots;
            push(vm, result);
            frame = &vm->frames[vm->frame_count - 1];
            break;
        }
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

#define ALLOCATE_OBJ(vm, type, object_type) \
    (type *)allocate_object(vm, sizeof(type), object_type)

static Obj *allocate_object(Vm *vm, size_t size, ObjType type)
{
    Obj *object = (Obj *)reallocate(NULL, 0, size);
    object->type = type;
    object->next = vm->objects;
    vm->objects = object;
    return object;
}

static ObjString *allocate_string(Vm *vm, char *chars, int length, uint32_t hash)
{
    ObjString *string = ALLOCATE_OBJ(vm, ObjString, OBJ_STRING);
    string->length = length;
    string->hash = hash;
    string->chars = chars;
    table_set(&vm->strings, string, NIL_VAL);
    return string;
}

static uint32_t hash_string(const char *chars, int length)
{
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++)
    {
        hash ^= (uint8_t)chars[i];
        hash *= 16777691;
    }
    return hash;
}

void init_vm(Vm *vm)
{
    reset_stack(vm);
    init_table(&vm->globals);
    init_table(&vm->strings);
    vm->objects = NULL;
    define_native(vm, "clock", clock_native);
}

void free_vm(Vm *vm)
{
    free_objects(vm);
    free_table(&vm->strings);
    free_table(&vm->globals);
}

InterpretResult interpret(Vm *vm, const char *source)
{
    Scanner scanner;
    init_scanner(&scanner, source);
    Parser parser;
    init_parser(&parser);
    Compiler compiler;
    init_compiler(&compiler, &scanner, &parser, vm, TYPE_SCRIPT);

    ObjFunction *function = compile(&compiler);

    if (function == NULL)
    {
        free_compiler(&compiler);
        free_scanner(&scanner);
        free_parser(&parser);
        return INTERPRET_COMPILE_ERROR;
    }

    push(vm, OBJ_VAL(function));
    call(vm, function, 0);

    InterpretResult result = run(vm);

    free_compiler(&compiler);
    free_scanner(&scanner);
    free_parser(&parser);
    return result;
}

ObjFunction *new_function(Vm *vm)
{
    ObjFunction *function = ALLOCATE_OBJ(vm, ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->name = NULL;
    init_chunk(&function->chunk);
    return function;
}

ObjNative *new_native(Vm *vm, NativeFn function)
{
    ObjNative *native = ALLOCATE_OBJ(vm, ObjNative, OBJ_NATIVE);
    native->function = function;
    return native;
}

ObjString *take_string(Vm *vm, char *chars, int length)
{
    uint32_t hash = hash_string(chars, length);
    ObjString *interned = table_find_string(&vm->strings, chars, length, hash);
    if (interned != NULL)
    {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }
    return allocate_string(vm, chars, length, hash);
}

ObjString *copy_string(Vm *vm, const char *chars, int length)
{
    uint32_t hash = hash_string(chars, length);
    ObjString *interned = table_find_string(&vm->strings, chars, length, hash);
    if (interned != NULL)
    {
        return interned;
    }
    char *heap_chars = ALLOCATE(char, length + 1);
    memcpy(heap_chars, chars, length);
    heap_chars[length] = '\0';
    return allocate_string(vm, heap_chars, length, hash);
}
