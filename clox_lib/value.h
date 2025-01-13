#ifndef CLOX_VALUE_H
#define CLOX_VALUE_H

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;
typedef struct Vm Vm;

typedef enum
{
    VAL_NIL,
    VAL_BOOL,
    VAL_NUMBER,
    VAL_OBJ,
} ValueType;

typedef struct
{
    ValueType type;
    union
    {
        bool boolean;
        double number;
        Obj *obj;
    } as;
} Value;

#define IS_NIL(value) ((value.type) == VAL_NIL)
#define IS_BOOL(value) ((value.type) == VAL_BOOL)
#define IS_NUMBER(value) ((value.type) == VAL_NUMBER)
#define IS_OBJ(value) ((value.type) == VAL_OBJ)

#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)
#define AS_OBJ(value) ((value).as.obj)

#define NIL_VAL ((Value){VAL_NIL, {.number = 0.}})
#define BOOL_VAL(value) ((Value){VAL_BOOL, {.boolean = value}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})
#define OBJ_VAL(value) ((Value){VAL_OBJ, {.obj = (Obj *)value}})

void print_value(Value value);
bool values_equal(Value a, Value b);

typedef struct
{
    int capacity;
    int count;
    Value *values;
} ValueArray;

void init_value_array(Vm *vm, ValueArray *array);
void free_value_array(Vm *vm, ValueArray *array);
void write_value_array(Vm *vm, ValueArray *array, Value value);

#endif
