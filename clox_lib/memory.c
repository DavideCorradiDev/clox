#include <stdlib.h>
#include "memory.h"
#include "compiler.h"
#include "vm.h"
#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

#ifdef DEBUG_LOG_GC
static void print_objects(Vm *vm)
{
    Obj *object = vm->objects;
    printf("Printing objects starting at %p...\n", object);
    while (object != NULL)
    {
        printf("%p ", (void *)object);
        print_value(OBJ_VAL(object));
        printf(", marked = %d", object->is_marked);
        printf(", next = %p", object->next);
        printf("\n");
        object = object->next;
    }
}
#endif

void *reallocate(Vm *vm, void *pointer, size_t old_size, size_t new_size)
{
    vm->bytes_allocated += new_size - old_size;
    if (new_size > old_size)
    {
#ifdef DEBUG_STRESS_GC
        collect_garbage(vm);
#endif
        if (vm->bytes_allocated > vm->next_gc)
        {
            collect_garbage(vm);
        }
    }

    if (new_size == 0)
    {
        free(pointer);
        return NULL;
    }

    void *result = NULL;
    if (old_size > 0)
    {
        result = realloc(pointer, new_size);
    }
    else
    {
        result = malloc(new_size);
    }
    if (result == NULL)
    {
        exit(1);
    }
    return result;
}

static void free_object(Vm *vm, Obj *object)
{
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void *)object, object->type);
#endif
    switch (object->type)
    {
    case OBJ_CLASS:
    {
        FREE(ObjClass, object);
        break;
    }
    case OBJ_FUNCTION:
    {
        ObjFunction *function = (ObjFunction *)object;
        free_chunk(vm, &function->chunk);
        FREE(ObjFunction, object);
        break;
    }
    case OBJ_INSTANCE:
    {
        ObjInstance *instance = (ObjInstance *)object;
        free_table(vm, &instance->fields);
        FREE(ObjInstance, object);
        break;
    }
    case OBJ_NATIVE:
    {
        FREE(ObjNative, object);
        break;
    }
    case OBJ_CLOSURE:
    {
        ObjClosure *closure = (ObjClosure *)object;
        FREE_ARRAY(ObjUpvalue *, closure->upvalues, closure->upvalue_count);
        FREE(ObjClosure, object);
        break;
    }
    case OBJ_STRING:
    {
        ObjString *string = (ObjString *)object;
        FREE_ARRAY(char, string->chars, string->length + 1);
        FREE(ObjString, object);
        break;
    }
    case OBJ_UPVALUE:
    {
        FREE(ObjUpvalue, object);
        break;
    }
    }
}

static void mark_roots(Vm *vm)
{
    for (Value *slot = vm->stack; slot < vm->stack_top; slot++)
    {
        mark_value(vm, *slot);
    }
    for (int i = 0; i < vm->frame_count; i++)
    {
        mark_object(vm, (Obj *)vm->frames[i].closure);
    }
    for (ObjUpvalue *upvalue = vm->open_upvalues; upvalue != NULL; upvalue = upvalue->next)
    {
        mark_object(vm, (Obj *)upvalue);
    }
    mark_table(vm, &vm->globals);
    mark_compiler_roots(vm->compiler);
}

static void mark_array(Vm *vm, ValueArray *array)
{
    for (int i = 0; i < array->count; i++)
    {
        mark_value(vm, array->values[i]);
    }
}

static void blacken_object(Vm *vm, Obj *object)
{
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void *)object);
    print_value(OBJ_VAL(object));
    printf("\n");
#endif
    switch (object->type)
    {
    case OBJ_CLASS:
    {
        mark_object(vm, object);
        break;
    }
    case OBJ_INSTANCE:
    {
        ObjInstance *instance = (ObjInstance *)object;
        mark_object(vm, object);
        mark_table(vm, &instance->fields);
        break;
    }
    case OBJ_CLOSURE:
    {
        ObjClosure *closure = (ObjClosure *)object;
        mark_object(vm, (Obj *)closure->function);
        for (int i = 0; i < closure->upvalue_count; i++)
        {
            mark_object(vm, (Obj *)closure->upvalues[i]);
        }
        break;
    }
    case OBJ_FUNCTION:
    {
        ObjFunction *function = (ObjFunction *)object;
        mark_object(vm, (Obj *)function->name);
        mark_array(vm, (&function->chunk.constants));
        break;
    }
    case OBJ_UPVALUE:
    {
        mark_value(vm, ((ObjUpvalue *)object)->closed);
        break;
    }
    case OBJ_NATIVE:
    case OBJ_STRING:
        break;
    }
}

static void trace_references(Vm *vm)
{
    while (vm->gray_count > 0)
    {
        Obj *object = vm->gray_stack[--vm->gray_count];
        blacken_object(vm, object);
    }
}

static void sweep(Vm *vm)
{
    Obj *previous = NULL;
    Obj *object = vm->objects;
    while (object != NULL)
    {
        if (object->is_marked)
        {
            object->is_marked = false;
            previous = object;
            object = object->next;
        }
        else
        {
            Obj *unreached = object;
            object = object->next;
            if (previous != NULL)
            {
                previous->next = object;
            }
            else
            {
                vm->objects = object;
            }
            free_object(vm, unreached);
        }
    }
}

void mark_object(Vm *vm, Obj *object)
{
    if (object == NULL)
    {
        return;
    }
    if (object->is_marked)
    {
        return;
    }
#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void *)object);
    print_value(OBJ_VAL(object));
    printf("\n");
#endif
    object->is_marked = true;

    if (vm->gray_capacity < vm->gray_count + 1)
    {
        vm->gray_capacity = GROW_CAPACITY(vm->gray_capacity);
        vm->gray_stack = (Obj **)realloc(vm->gray_stack, sizeof(Obj *) * vm->gray_capacity);
        if (vm->gray_stack == NULL)
        {
            exit(1);
        }
    }
    vm->gray_stack[vm->gray_count++] = object;
}

void mark_value(Vm *vm, Value value)
{
    if (IS_OBJ(value))
    {
        mark_object(vm, AS_OBJ(value));
    }
}

void collect_garbage(Vm *vm)
{
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
    size_t before = vm->bytes_allocated;
    print_objects(vm);
#endif

    mark_roots(vm);
    trace_references(vm);
    table_remove_white(&vm->strings);
    sweep(vm);

    vm->next_gc = vm->bytes_allocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
           before - vm->bytes_allocated, before, vm->bytes_allocated,
           vm->next_gc);
#endif
}

void free_objects(Vm *vm)
{
    Obj *object = vm->objects;
    while (object != NULL)
    {
        Obj *next = object->next;
        free_object(vm, object);
        object = next;
    }
    free(vm->gray_stack);
}
