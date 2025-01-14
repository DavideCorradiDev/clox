#include "object.h"
#include "memory.h"
#include "vm.h"

#include <string.h>
#include <stdio.h>

#define ALLOCATE_OBJ(vm, type, object_type) \
    (type *)allocate_object(vm, sizeof(type), object_type)

static Obj *allocate_object(Vm *vm, size_t size, ObjType type)
{
    Obj *object = (Obj *)reallocate(vm, NULL, 0, size);
    object->type = type;
    object->is_marked = false;
    object->next = vm->objects;
    vm->objects = object;
#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void *)object, size, type);
#endif
    return object;
}

static ObjString *allocate_string(Vm *vm, char *chars, int length, uint32_t hash)
{
    ObjString *string = ALLOCATE_OBJ(vm, ObjString, OBJ_STRING);
    string->length = length;
    string->hash = hash;
    string->chars = chars;
    push(vm, OBJ_VAL(string));
    table_set(vm, &vm->strings, string, NIL_VAL);
    pop(vm);
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

static void print_function(ObjFunction *function)
{
    if (function->name == NULL)
    {
        printf("<script>");
        return;
    }
    printf("<fn %s>", function->name->chars);
}

ObjBoundMethod *new_bound_method(Vm *vm, Value receiver, ObjClosure *method)
{
    ObjBoundMethod *bound = ALLOCATE_OBJ(vm, ObjBoundMethod, OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

ObjClass *new_class(Vm *vm, ObjString *name)
{
    ObjClass *klass = ALLOCATE_OBJ(vm, ObjClass, OBJ_CLASS);
    klass->name = name;
    init_table(vm, &klass->methods);
    return klass;
}

ObjInstance *new_instance(Vm *vm, ObjClass *klass)
{
    ObjInstance *instance = ALLOCATE_OBJ(vm, ObjInstance, OBJ_INSTANCE);
    instance->klass = klass;
    init_table(vm, &instance->fields);
    return instance;
}

ObjFunction *new_function(Vm *vm)
{
    ObjFunction *function = ALLOCATE_OBJ(vm, ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->upvalue_count = 0;
    function->name = NULL;
    init_chunk(vm, &function->chunk);
    return function;
}

ObjNative *new_native(Vm *vm, int arity, NativeFn function)
{
    ObjNative *native = ALLOCATE_OBJ(vm, ObjNative, OBJ_NATIVE);
    native->arity = arity;
    native->function = function;
    return native;
}

ObjClosure *new_closure(Vm *vm, ObjFunction *function)
{
    ObjUpvalue **upvalues = ALLOCATE(ObjUpvalue *, function->upvalue_count);
    for (int i = 0; i < function->upvalue_count; i++)
    {
        upvalues[i] = NULL;
    }
    ObjClosure *closure = ALLOCATE_OBJ(vm, ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalue_count = function->upvalue_count;
    return closure;
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

ObjUpvalue *new_upvalue(Vm *vm, Value *slot)
{
    ObjUpvalue *upvalue = ALLOCATE_OBJ(vm, ObjUpvalue, OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->closed = NIL_VAL;
    upvalue->next = NULL;
    return upvalue;
}

void print_object(Value value)
{
    switch (OBJ_TYPE(value))
    {
    case OBJ_BOUND_METHOD:
        print_function(AS_BOUND_METHOD(value)->method->function);
        break;
    case OBJ_CLASS:
        printf("%s", AS_CLASS(value)->name->chars);
        break;
    case OBJ_FUNCTION:
        print_function(AS_FUNCTION(value));
        break;
    case OBJ_INSTANCE:
        printf("%s instance", AS_INSTANCE(value)->klass->name->chars);
        break;
    case OBJ_NATIVE:
        printf("<native fn>");
        break;
    case OBJ_CLOSURE:
        print_function(AS_CLOSURE(value)->function);
        break;
    case OBJ_STRING:
        printf("%s", AS_CSTRING(value));
        break;
    case OBJ_UPVALUE:
        printf("upvalue");
        break;
    }
}
