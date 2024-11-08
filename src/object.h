#ifndef CLOX_OBJECT_H
#define CLOX_OBJECT_H

#include "common.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_STRING(value) is_obj_type(value, OBJ_STRING)

#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)

typedef enum
{
    OBJ_STRING,
} ObjType;

struct Obj
{
    ObjType type;
    struct Obj *next;
};

void print_object(Value value);

struct ObjString
{
    Obj obj;
    int length;
    char *chars;
};

ObjString *take_string(Vm *vm, char *chars, int length);
ObjString *copy_string(Vm *vm, const char *chars, int length);

static inline bool is_obj_type(Value value, ObjType type)
{
    return IS_OBJ(value) && OBJ_TYPE(value) == type;
}

#endif
