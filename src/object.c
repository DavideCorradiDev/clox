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

void print_object(Value value)
{
    switch (OBJ_TYPE(value))
    {
    case OBJ_STRING:
        printf("%s", AS_CSTRING(value));
        break;
    }
}

ObjString *make_string(Vm *vm, int length)
{
    ObjString *string = (ObjString *)allocate_object(vm, sizeof(ObjString) + length + 1, OBJ_STRING);
    string->length = length;
    return string;
}

ObjString *copy_string(Vm *vm, const char *chars, int length)
{
    ObjString *string = make_string(vm, length);
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    return string;
}
