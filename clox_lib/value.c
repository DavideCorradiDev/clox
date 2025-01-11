#include <stdio.h>
#include <string.h>

#include "value.h"
#include "object.h"
#include "memory.h"
#include "vm.h"

void print_value(Value value)
{
    switch (value.type)
    {
    case VAL_NIL:
        printf("nil");
        break;
    case VAL_BOOL:
        printf(AS_BOOL(value) ? "true" : "false");
        break;
    case VAL_NUMBER:
        printf("%g", AS_NUMBER(value));
        break;
    case VAL_OBJ:
        print_object(value);
        break;
    }
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

void print_object(Value value)
{
    switch (OBJ_TYPE(value))
    {
    case OBJ_FUNCTION:
        print_function(AS_FUNCTION(value));
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

bool values_equal(Value a, Value b)
{
    if (a.type != b.type)
    {
        return false;
    }
    switch (a.type)
    {
    case VAL_NIL:
        return true;
    case VAL_BOOL:
        return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NUMBER:
        return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ:
        return AS_OBJ(a) == AS_OBJ(b);
    default:
        return false; // Unreachable.
    }
}

void init_value_array(Vm *vm, ValueArray *array)
{
    array->capacity = 0;
    array->count = 0;
    array->values = NULL;
}

void free_value_array(Vm *vm, ValueArray *array)
{
    FREE_ARRAY(Value, array->values, array->capacity);
    init_value_array(vm, array);
}

void write_value_array(Vm *vm, ValueArray *array, Value value)
{
    if (array->capacity < array->count + 1)
    {
        int old_capacity = array->capacity;
        array->capacity = GROW_CAPACITY(old_capacity);
        array->values = GROW_ARRAY(Value, array->values, old_capacity, array->capacity);
    }
    array->values[array->count] = value;
    array->count++;
}
