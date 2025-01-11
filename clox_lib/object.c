#include "object.h"
#include "memory.h"
#include "vm.h"

#include <string.h>
#ifdef DEBUG_LOG_GC
#include <stdio.h>
#endif

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
