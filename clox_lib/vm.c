#include "vm.h"

#include "compiler.h"
#include "memory.h"
#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif

#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static bool clock_native(Vm *vm, int arg_count, Value *args)
{
    args[-1] = NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
    return true;
}

static bool err_native(Vm *vm, int arg_count, Value *args)
{
    args[-1] = OBJ_VAL(copy_string(vm, "Error!", 6));
    return false;
}

static bool has_field_native(Vm *vm, int arg_count, Value *args)
{
    if (!IS_INSTANCE(args[0]))
    {
        args[-1] = OBJ_VAL(copy_string(vm, "Expect instance.", 16));
        return false;
    }
    if (!IS_STRING(args[1]))
    {
        args[-1] = OBJ_VAL(copy_string(vm, "Expect string.", 14));
        return false;
    }
    ObjInstance *instance = AS_INSTANCE(args[0]);
    Value dummy;
    args[-1] = BOOL_VAL(table_get(&instance->fields, AS_STRING(args[1]), &dummy));
    return true;
}

static bool delete_field_native(Vm *vm, int arg_count, Value *args)
{
    if (!IS_INSTANCE(args[0]))
    {
        args[-1] = OBJ_VAL(copy_string(vm, "Expect instance.", 16));
        return false;
    }
    if (!IS_STRING(args[1]))
    {
        args[-1] = OBJ_VAL(copy_string(vm, "Expect string.", 14));
        return false;
    }
    ObjInstance *instance = AS_INSTANCE(args[0]);
    table_delete(&instance->fields, AS_STRING(args[1]));
    args[-1] = NIL_VAL;
    return true;
}

void push(Vm *vm, Value value)
{
    *vm->stack_top = value;
    vm->stack_top++;
}

Value pop(Vm *vm)
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
    vm->open_upvalues = NULL;
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
        ObjFunction *function = frame->closure->function;
        size_t instruction = frame->ip - frame->closure->function->chunk.code - 1;
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

static void define_native(Vm *vm, const char *name, int arity, NativeFn function)
{
    push(vm, OBJ_VAL(copy_string(vm, name, (int)strlen(name))));
    push(vm, OBJ_VAL(new_native(vm, arity, function)));
    table_set(vm, &vm->globals, AS_STRING(vm->stack[0]), vm->stack[1]);
    pop(vm);
    pop(vm);
}

static bool call(Vm *vm, ObjClosure *closure, int arg_count)
{
    if (arg_count != closure->function->arity)
    {
        runtime_error(vm, "Expected %d arguments but got %d.", closure->function->arity, arg_count);
        return false;
    }

    if (vm->frame_count == FRAMES_MAX)
    {
        runtime_error(vm, "Stack overflow.");
        return false;
    }

    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm->stack_top - arg_count - 1;
    return true;
}

static bool call_value(Vm *vm, Value callee, int arg_count)
{
    if (IS_OBJ(callee))
    {
        switch (OBJ_TYPE(callee))
        {
        case OBJ_BOUND_METHOD:
        {
            ObjBoundMethod *bound = AS_BOUND_METHOD(callee);
            vm->stack_top[-arg_count - 1] = bound->receiver;
            return call(vm, bound->method, arg_count);
            break;
        }
        case OBJ_CLASS:
        {
            ObjClass *klass = AS_CLASS(callee);
            vm->stack_top[-arg_count - 1] = OBJ_VAL(new_instance(vm, klass));
            Value initializer;
            if (table_get(&klass->methods, vm->init_string, &initializer))
            {
                return call(vm, AS_CLOSURE(initializer), arg_count);
            }
            else if (arg_count != 0)
            {
                runtime_error(vm, "Expected 0 arguments but got %d.", arg_count);
                return false;
            }
            return true;
            break;
        }
        case OBJ_NATIVE:
        {
            ObjNative *native = AS_NATIVE(callee);

            if (arg_count != native->arity)
            {
                runtime_error(vm, "Expected %d arguments but got %d.", native->arity, arg_count);
                return false;
            }

            if (native->function(vm, arg_count, vm->stack_top - arg_count))
            {
                vm->stack_top -= arg_count;
                return true;
            }
            else
            {
                runtime_error(vm, AS_STRING(vm->stack_top[-arg_count - 1])->chars);
                return false;
            }
        }
        case OBJ_CLOSURE:
            return call(vm, AS_CLOSURE(callee), arg_count);
        default:
            break;
        }
    }
    runtime_error(vm, "Can only call functions and classes.");
    return false;
}

static bool invoke_from_class(Vm *vm, ObjClass *klass, ObjString *name, int arg_count)
{
    Value method;
    if (!table_get(&klass->methods, name, &method))
    {
        runtime_error(vm, "Undefined property '%s'.", name->chars);
        return false;
    }
    return call(vm, AS_CLOSURE(method), arg_count);
}

static bool invoke(Vm *vm, ObjString *name, int arg_count)
{
    Value receiver = peek(vm, arg_count);
    if (!IS_INSTANCE(receiver))
    {
        runtime_error(vm, "Only instances have methods.");
        return false;
    }
    ObjInstance *instance = AS_INSTANCE(receiver);
    Value value;
    if (table_get(&instance->fields, name, &value))
    {
        vm->stack_top[-arg_count - 1] = value;
        return call_value(vm, value, arg_count);
    }
    return invoke_from_class(vm, instance->klass, name, arg_count);
}

static bool bind_method(Vm *vm, ObjClass *klass, ObjString *name)
{
    Value method;
    if (!table_get(&klass->methods, name, &method))
    {
        runtime_error(vm, "Undefined property %s.");
        return false;
    }

    ObjBoundMethod *bound = new_bound_method(vm, peek(vm, 0), AS_CLOSURE(method));
    pop(vm);
    push(vm, OBJ_VAL(bound));
    return true;
}

static ObjUpvalue *capture_upvalue(Vm *vm, Value *local)
{
    ObjUpvalue *prev_upvalue = NULL;
    ObjUpvalue *upvalue = vm->open_upvalues;
    while (upvalue != NULL && upvalue->location > local)
    {
        prev_upvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local)
    {
        return upvalue;
    }

    ObjUpvalue *created_upvalue = new_upvalue(vm, local);
    created_upvalue->next = upvalue;
    if (prev_upvalue == NULL)
    {
        vm->open_upvalues = created_upvalue;
    }
    else
    {
        prev_upvalue->next = created_upvalue;
    }
    return created_upvalue;
}

static void close_upvalues(Vm *vm, Value *last)
{
    while (vm->open_upvalues != NULL && vm->open_upvalues->location >= last)
    {
        ObjUpvalue *upvalue = vm->open_upvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm->open_upvalues = upvalue->next;
    }
}

static void define_method(Vm *vm, ObjString *name)
{
    Value method = peek(vm, 0);
    ObjClass *klass = AS_CLASS(peek(vm, 1));
    table_set(vm, &klass->methods, name, method);
    pop(vm);
}

static bool is_falsey(Value value)
{
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(Vm *vm)
{
    ObjString *b = AS_STRING(peek(vm, 0));
    ObjString *a = AS_STRING(peek(vm, 1));
    int length = a->length + b->length;
    char *chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString *result = take_string(vm, chars, length);
    pop(vm);
    pop(vm);
    push(vm, OBJ_VAL(result));
}

static InterpretResult run(Vm *vm)
{
    CallFrame *frame = &vm->frames[vm->frame_count - 1];
    register uint8_t *ip = frame->ip;
#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2, ((uint16_t)(ip[-2]) << 8 | (uint16_t)(ip[-1])))
#define READ_3_BYTES() (ip += 3, ((uint32_t)(ip[-2]) << 16 | (uint32_t)(ip[-2]) << 8 | (uint32_t)(ip[-1])))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG() (frame->closure->function->chunk.constants.values[READ_3_BYTES()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(value_type, op)                               \
    do                                                          \
    {                                                           \
        if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) \
        {                                                       \
            frame->ip = ip;                                     \
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

        disassemble_instruction(&frame->closure->function->chunk, (int)(ip - frame->closure->function->chunk.code));
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
                frame->ip = ip;
                runtime_error(vm, "Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            push(vm, value);
            break;
        }
        case OP_DEFINE_GLOBAL:
        {
            ObjString *name = READ_STRING();
            table_set(vm, &vm->globals, name, peek(vm, 0));
            pop(vm);
            break;
        }
        case OP_SET_GLOBAL:
        {
            ObjString *name = READ_STRING();
            if (table_set(vm, &vm->globals, name, peek(vm, 0)))
            {
                table_delete(&vm->globals, name);
                frame->ip = ip;
                runtime_error(vm, "Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_POP:
            pop(vm);
            break;
        case OP_GET_UPVALUE:
        {
            uint8_t slot = READ_BYTE();
            push(vm, *frame->closure->upvalues[slot]->location);
            break;
        }
        case OP_SET_UPVALUE:
        {
            uint8_t slot = READ_BYTE();
            *frame->closure->upvalues[slot]->location = peek(vm, 0);
            break;
        }
        case OP_GET_PROPERTY:
        {
            if (!IS_INSTANCE(peek(vm, 0)))
            {
                runtime_error(vm, "Only instances have properties.");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjInstance *instance = AS_INSTANCE(peek(vm, 0));
            ObjString *name = READ_STRING();

            Value value;
            if (table_get(&instance->fields, name, &value))
            {
                pop(vm);
                push(vm, value);
                break;
            }

            if (!bind_method(vm, instance->klass, name))
            {
                return INTERPRET_RUNTIME_ERROR;
            }

            break;
        }
        case OP_SET_PROPERTY:
        {
            if (!IS_INSTANCE(peek(vm, 1)))
            {
                runtime_error(vm, "Only instances have properties.");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjInstance *instance = AS_INSTANCE(peek(vm, 1));
            table_set(vm, &instance->fields, READ_STRING(), peek(vm, 0));
            Value value = pop(vm);
            pop(vm);
            push(vm, value);
            break;
        }
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
                frame->ip = ip;
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
                frame->ip = ip;
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
            ip += offset;
            break;
        }
        case OP_JUMP_IF_FALSE:
        {
            uint16_t offset = READ_SHORT();
            if (is_falsey(peek(vm, 0)))
            {
                ip += offset;
            }
            break;
        }
        case OP_LOOP:
        {
            uint16_t offset = READ_SHORT();
            ip -= offset;
            break;
        }
        case OP_CALL:
        {
            int arg_count = READ_BYTE();
            frame->ip = ip;
            if (!call_value(vm, peek(vm, arg_count), arg_count))
            {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm->frames[vm->frame_count - 1];
            ip = frame->ip;
            break;
        }
        case OP_INVOKE:
        {
            frame->ip = ip;
            ObjString *method = READ_STRING();
            int arg_count = READ_BYTE();
            if (!invoke(vm, method, arg_count))
            {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm->frames[vm->frame_count - 1];
            ip = frame->ip;
            break;
        }
        case OP_CLOSURE:
        {
            ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
            ObjClosure *closure = new_closure(vm, function);
            push(vm, OBJ_VAL(closure));
            for (int i = 0; i < closure->upvalue_count; i++)
            {
                uint8_t is_local = READ_BYTE();
                uint8_t index = READ_BYTE();
                if (is_local)
                {
                    closure->upvalues[i] = capture_upvalue(vm, frame->slots + index);
                }
                else
                {
                    closure->upvalues[i] = frame->closure->upvalues[index];
                }
            }
            break;
        }
        case OP_CLOSE_UPVALUE:
        {
            close_upvalues(vm, vm->stack_top - 1);
            pop(vm);
            break;
        }
        case OP_RETURN:
        {
            Value result = pop(vm);
            close_upvalues(vm, frame->slots);
            vm->frame_count--;
            if (vm->frame_count == 0)
            {
                pop(vm);
                return INTERPRET_OK;
            }

            vm->stack_top = frame->slots;
            push(vm, result);
            frame = &vm->frames[vm->frame_count - 1];
            ip = frame->ip;
            break;
        }
        case OP_CLASS:
        {
            push(vm, OBJ_VAL(new_class(vm, READ_STRING())));
            break;
        }
        case OP_METHOD:
        {
            define_method(vm, READ_STRING());
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

void init_vm(Vm *vm)
{
    vm->compiler = NULL;
    reset_stack(vm);
    init_table(vm, &vm->globals);
    init_table(vm, &vm->strings);
    vm->bytes_allocated = 0;
    vm->next_gc = 1024 * 1024;
    vm->objects = NULL;
    vm->gray_capacity = 0;
    vm->gray_count = 0;
    vm->gray_stack = NULL;
    vm->init_string = NULL;
    vm->init_string = copy_string(vm, "init", 4);

    define_native(vm, "clock", 0, clock_native);
    define_native(vm, "err", 0, err_native);
    define_native(vm, "has_field", 2, has_field_native);
    define_native(vm, "delete_field", 2, delete_field_native);
}

void free_vm(Vm *vm)
{
    vm->compiler = NULL;
    vm->init_string = NULL;
    free_objects(vm);
    free_table(vm, &vm->strings);
    free_table(vm, &vm->globals);
}

InterpretResult interpret(Vm *vm, const char *source)
{
    Scanner scanner;
    init_scanner(&scanner, source);
    Parser parser;
    init_parser(&parser);
    Compiler compiler;
    init_compiler(&compiler, &scanner, &parser, vm, TYPE_SCRIPT);
    vm->compiler = &compiler;

    ObjFunction *function = compile(&compiler);

    if (function == NULL)
    {
        free_compiler(&compiler);
        free_scanner(&scanner);
        free_parser(&parser);
        return INTERPRET_COMPILE_ERROR;
    }

    push(vm, OBJ_VAL(function));
    ObjClosure *closure = new_closure(vm, function);
    pop(vm);
    push(vm, OBJ_VAL(closure));
    call(vm, closure, 0);

    InterpretResult result = run(vm);

    free_compiler(&compiler);
    vm->compiler = NULL;

    free_scanner(&scanner);
    free_parser(&parser);
    return result;
}
