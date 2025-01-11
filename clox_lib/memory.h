#ifndef CLOX_MEMORY_H
#define CLOX_MEMORY_H

#include "common.h"
#include "object.h"
#include "vm.h"

#define ALLOCATE(type, count) \
    (type *)reallocate(vm, NULL, 0, sizeof(type) * (count))

#define FREE(type, pointer) \
    reallocate(vm, pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : capacity * 2);

#define GROW_ARRAY(type, pointer, old_count, new_count) \
    (type *)reallocate(vm, pointer, sizeof(type) * (old_count), sizeof(type) * (new_count));

#define FREE_ARRAY(type, pointer, old_count) \
    reallocate(vm, pointer, sizeof(type) * (old_count), 0);

void *reallocate(Vm *vm, void *pointer, size_t old_size, size_t new_size);
void mark_object(Vm *vm, Obj *object);
void mark_value(Vm *vm, Value value);
void collect_garbage(Vm *vm);
void free_objects(Vm *vm);

#endif
