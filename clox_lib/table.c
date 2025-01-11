#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define TABLE_MAX_LOAD 0.75

void init_table(Vm *vm, Table *table)
{
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void free_table(Vm *vm, Table *table)
{
    FREE_ARRAY(Entry, table->entries, table->capacity);
    init_table(vm, table);
}

static Entry *find_entry(Entry *entries, size_t capacity, ObjString *key)
{
    uint32_t index = key->hash % capacity;
    Entry *tombstone = NULL;
    for (;;)
    {
        Entry *entry = &entries[index];
        if (entry->key == NULL)
        {
            if (IS_NIL(entry->value))
            {
                return tombstone != NULL ? tombstone : entry;
            }
            else
            {
                if (tombstone == NULL)
                {
                    tombstone = entry;
                }
            }
        }
        else if (entry->key == key)
        {
            return entry;
        }
        index = (index + 1) % capacity;
    }
}

static void adjust_capacity(Vm *vm, Table *table, int capacity)
{
    Entry *entries = ALLOCATE(Entry, capacity);
    for (int i = 0; i < capacity; i++)
    {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;
    for (int i = 0; i < table->capacity; i++)
    {
        Entry *entry = &table->entries[i];
        if (entry->key == NULL)
        {
            continue;
        }
        Entry *dest = find_entry(entries, capacity, entry->key);
        *dest = *entry;
        table->count++;
    }

    FREE_ARRAY(Entry, table->entries, table->capacity);

    table->entries = entries;
    table->capacity = capacity;
}

bool table_get(Table *table, ObjString *key, Value *value)
{
    if (table->count == 0)
    {
        return false;
    }

    Entry *entry = find_entry(table->entries, table->capacity, key);
    if (entry->key == NULL)
    {
        return false;
    }

    *value = entry->value;
    return true;
}

bool table_set(Vm *vm, Table *table, ObjString *key, Value value)
{
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD)
    {
        int capacity = GROW_CAPACITY(table->capacity);
        adjust_capacity(vm, table, capacity);
    }

    Entry *entry = find_entry(table->entries, table->capacity, key);
    bool is_new_key = entry->key == NULL;
    if (is_new_key && IS_NIL(entry->value))
    {
        table->count++;
    }
    entry->key = key;
    entry->value = value;
    return is_new_key;
}

bool table_delete(Table *table, ObjString *key)
{
    if (table->count == 0)
    {
        return false;
    }

    Entry *entry = find_entry(table->entries, table->capacity, key);
    if (entry->key == NULL)
    {
        return false;
    }

    entry->key = NULL;
    entry->value = BOOL_VAL(true);
    return true;
}

void table_add_all(Vm *vm, Table *from, Table *to)
{
    for (int i = 0; i < from->capacity; i++)
    {
        Entry *entry = &from->entries[i];
        if (entry->key != NULL)
        {
            table_set(vm, to, entry->key, entry->value);
        }
    }
}

ObjString *table_find_string(Table *table, const char *chars, int length, uint32_t hash)
{
    if (table->count == 0)
    {
        return NULL;
    }

    uint32_t index = hash % table->capacity;
    for (;;)
    {
        Entry *entry = &table->entries[index];
        if (entry->key == NULL)
        {
            if (IS_NIL(entry->value))
            {
                return NULL;
            }
        }
        else if (entry->key->length == length && entry->key->hash == hash && memcmp(entry->key->chars, chars, length) == 0)
        {
            return entry->key;
        }
        index = (index + 1) % table->capacity;
    }
}

void table_remove_white(Table *table)
{
    for (int i = 0; i < table->capacity; i++)
    {
        Entry *entry = &table->entries[i];
        if (entry->key != NULL && !entry->key->obj.is_marked)
        {
            table_delete(table, entry->key);
        }
    }
}

void mark_table(Vm *vm, Table *table)
{
    for (int i = 0; i < table->capacity; i++)
    {
        Entry *entry = &table->entries[i];
        mark_object(vm, (Obj *)entry->key);
        mark_value(vm, entry->value);
    }
}

void print_table(Table *table)
{
    for (int i = 0; i < table->capacity; ++i)
    {
        Entry *entry = &table->entries[i];
        if (entry->key != NULL)
        {
            printf("[ ");
            printf("%s -> ", entry->key->chars);
            print_value(entry->value);
            printf(" ]");
        }
    }
}
