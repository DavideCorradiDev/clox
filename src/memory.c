#include <stdlib.h>
#include "memory.h"
#include "vm.h"

void *reallocate(void *pointer, size_t old_size, size_t new_size)
{
    if (new_size == 0)
    {
        free(pointer);
        return NULL;
    }

    void *result = realloc(pointer, new_size);
    if (result == NULL)
    {
        exit(1);
    }
    return result;
}

static void free_object(Obj *object)
{
    switch (object->type)
    {
    case OBJ_STRING:
    {
        ObjString *string = (ObjString *)object;
        reallocate(object, sizeof(ObjString) + string->length + 1, 0);
        break;
    }
    }
}

void free_objects(Vm *vm)
{
    Obj *object = vm->objects;
    while (object != NULL)
    {
        Obj *next = object->next;
        free_object(object);
        object = next;
    }
}
