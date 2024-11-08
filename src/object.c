#include <stdio.h>
#include <string.h>

#include "memory.h"
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

static ObjString *allocate_string(Vm *vm, char *chars, int length)
{
    ObjString *string = ALLOCATE_OBJ(vm, ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    return string;
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
    return allocate_string(vm, chars, length);
}

ObjString *copy_string(Vm *vm, const char *chars, int length)
{
    char *heap_chars = ALLOCATE(char, length + 1);
    memcpy(heap_chars, chars, length);
    heap_chars[length] = '\0';
    return allocate_string(vm, heap_chars, length);
}
