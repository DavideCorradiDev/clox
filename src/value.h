#ifndef CLOX_VALUE_H
#define CLOX_VALUE_H

#include "common.h"

void print_value(Value value);
bool values_equal(Value a, Value b);

typedef struct
{
    int capacity;
    int count;
    Value *values;
} ValueArray;

void init_value_array(ValueArray *array);
void free_value_array(ValueArray *array);
void write_value_array(ValueArray *array, Value value);

#endif
