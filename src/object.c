#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "table.h"
#include "value.h"

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

void print_object(Value value)
{
    switch (OBJ_TYPE(value))
    {
    case OBJ_STRING:
        printf("%s", AS_CSTRING(value));
        break;
    }
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
