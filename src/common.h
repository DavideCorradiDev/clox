#ifndef CLOX_COMMON_H
#define CLOX_COMMON_H

#define DEBUG_PRINT_CODE
#define DEBUG_TRACE_EXECUTION

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    VAL_NIL,
    VAL_BOOL,
    VAL_NUMBER,
} ValueType;

typedef struct
{
    ValueType type;
    union
    {
        bool boolean;
        double number;
    } as;
} Value;

#define IS_NIL(value) ((value.type) == VAL_NIL)
#define IS_BOOL(value) ((value.type) == VAL_BOOL)
#define IS_NUMBER(value) ((value.type) == VAL_NUMBER)

#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)

#define NIL_VAL ((Value){VAL_NIL, {.number = 0.}})
#define BOOL_VAL(value) ((Value){VAL_BOOL, {.boolean = value}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})

#endif
