#ifndef CLOX_OBJECT_H
#define CLOX_OBJECT_H

#include "common.h"
#include "chunk.h"
#include "table.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_BOUND_METHOD(value) is_obj_type(value, OBJ_BOUND_METHOD);
#define IS_CLASS(value) is_obj_type(value, OBJ_CLASS)
#define IS_FUNCTION(value) is_obj_type(value, OBJ_FUNCTION)
#define IS_INSTANCE(value) is_obj_type(value, OBJ_INSTANCE)
#define IS_NATIVE(value) is_obj_type(value, OBJ_NATIVE)
#define IS_CLOSURE(value) is_obj_type(value, OBJ_CLOSURE)
#define IS_STRING(value) is_obj_type(value, OBJ_STRING)

#define AS_BOUND_METHOD(value) ((ObjBoundMethod *)AS_OBJ(value))
#define AS_CLASS(value) ((ObjClass *)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance *)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNative *)AS_OBJ(value)))
#define AS_CLOSURE(value) (((ObjClosure *)AS_OBJ(value)))
#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)

typedef struct Vm Vm;

typedef enum
{
    OBJ_BOUND_METHOD,
    OBJ_CLASS,
    OBJ_FUNCTION,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_CLOSURE,
    OBJ_STRING,
    OBJ_UPVALUE,
} ObjType;

typedef struct Obj
{
    ObjType type;
    bool is_marked;
    struct Obj *next;
} Obj;

typedef struct ObjFunction
{
    Obj obj;
    int arity;
    int upvalue_count;
    Chunk chunk;
    ObjString *name;
} ObjFunction;

typedef struct Vm Vm;
typedef bool (*NativeFn)(Vm *vm, int arg_count, Value *args);

typedef struct ObjNative
{
    Obj obj;
    int arity;
    NativeFn function;
} ObjNative;

typedef struct ObjUpvalue
{
    Obj obj;
    Value *location;
    Value closed;
    struct ObjUpvalue *next;
} ObjUpvalue;

typedef struct ObjClosure
{
    Obj obj;
    ObjFunction *function;
    ObjUpvalue **upvalues;
    int upvalue_count;
} ObjClosure;

typedef struct ObjClass
{
    Obj obj;
    ObjString *name;
    Table methods;
} ObjClass;

typedef struct ObjInstance
{
    Obj obj;
    ObjClass *klass;
    Table fields;
} ObjInstance;

typedef struct ObjBoundMethod
{
    Obj obj;
    Value receiver;
    ObjClosure *method;
} ObjBoundMethod;

typedef struct ObjString
{
    Obj obj;
    int length;
    uint32_t hash;
    char *chars;
} ObjString;

static inline bool is_obj_type(Value value, ObjType type)
{
    return IS_OBJ(value) && OBJ_TYPE(value) == type;
}

ObjBoundMethod *new_bound_method(Vm *vm, Value receiver, ObjClosure *method);
ObjClass *new_class(Vm *vm, ObjString *name);
ObjFunction *new_function(Vm *vm);
ObjInstance *new_instance(Vm *vm, ObjClass *klass);
ObjNative *new_native(Vm *vm, int arity, NativeFn function);
ObjClosure *new_closure(Vm *vm, ObjFunction *function);
ObjString *take_string(Vm *vm, char *chars, int length);
ObjString *copy_string(Vm *vm, const char *chars, int length);
ObjUpvalue *new_upvalue(Vm *vm, Value *slot);
void print_object(Value value);

#endif
