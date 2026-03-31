#pragma once
#include "common.h"
#include "value.h"

typedef struct {
    ObjString *key;   // NULL = empty slot
    Value      value;
} Entry;

typedef struct {
    int    count;     // number of live entries + tombstones
    int    capacity;
    Entry *entries;
} Table;

void  table_init(Table *t);
void  table_free(Table *t);
bool  table_get(Table *t, ObjString *key, Value *out);
bool  table_set(Table *t, ObjString *key, Value value);
bool  table_delete(Table *t, ObjString *key);
void  table_copy(Table *src, Table *dst);

// Used by string interning: find an existing string by raw chars + hash
ObjString *table_find_string(Table *t, const char *chars, int length,
                              uint32_t hash);
