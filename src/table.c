#include "table.h"
#include "memory.h"
#include "object.h"

#define TABLE_MAX_LOAD 0.75

void table_init(Table *t) {
    t->count    = 0;
    t->capacity = 0;
    t->entries  = NULL;
}

void table_free(Table *t) {
    FREE_ARRAY(Entry, t->entries, t->capacity);
    table_init(t);
}

static Entry *find_entry(Entry *entries, int capacity, ObjString *key) {
    uint32_t index = key->hash & (uint32_t)(capacity - 1);
    Entry *tombstone = NULL;
    for (;;) {
        Entry *e = &entries[index];
        if (e->key == NULL) {
            if (IS_NIL(e->value)) {
                // Empty slot — return tombstone if we passed one
                return tombstone != NULL ? tombstone : e;
            } else {
                // Tombstone
                if (tombstone == NULL) tombstone = e;
            }
        } else if (e->key == key) {
            return e;
        }
        index = (index + 1) & (uint32_t)(capacity - 1);
    }
}

static void adjust_capacity(Table *t, int new_cap) {
    Entry *new_entries = ALLOCATE(Entry, new_cap);
    for (int i = 0; i < new_cap; i++) {
        new_entries[i].key   = NULL;
        new_entries[i].value = NIL_VAL;
    }
    // Reinsert live entries (skip tombstones)
    t->count = 0;
    for (int i = 0; i < t->capacity; i++) {
        Entry *src = &t->entries[i];
        if (src->key == NULL) continue;
        Entry *dst = find_entry(new_entries, new_cap, src->key);
        dst->key   = src->key;
        dst->value = src->value;
        t->count++;
    }
    FREE_ARRAY(Entry, t->entries, t->capacity);
    t->entries  = new_entries;
    t->capacity = new_cap;
}

bool table_get(Table *t, ObjString *key, Value *out) {
    if (t->count == 0) return false;
    Entry *e = find_entry(t->entries, t->capacity, key);
    if (e->key == NULL) return false;
    *out = e->value;
    return true;
}

bool table_set(Table *t, ObjString *key, Value value) {
    if (t->count + 1 > t->capacity * TABLE_MAX_LOAD) {
        int new_cap = t->capacity < 8 ? 8 : t->capacity * 2;
        adjust_capacity(t, new_cap);
    }
    Entry *e    = find_entry(t->entries, t->capacity, key);
    bool is_new = (e->key == NULL);
    if (is_new && IS_NIL(e->value)) t->count++; // not a tombstone
    e->key   = key;
    e->value = value;
    return is_new;
}

bool table_delete(Table *t, ObjString *key) {
    if (t->count == 0) return false;
    Entry *e = find_entry(t->entries, t->capacity, key);
    if (e->key == NULL) return false;
    // Place tombstone
    e->key   = NULL;
    e->value = BOOL_VAL(true);
    return true;
}

void table_copy(Table *src, Table *dst) {
    for (int i = 0; i < src->capacity; i++) {
        Entry *e = &src->entries[i];
        if (e->key != NULL) table_set(dst, e->key, e->value);
    }
}

ObjString *table_find_string(Table *t, const char *chars, int length,
                              uint32_t hash) {
    if (t->count == 0) return NULL;
    uint32_t index = hash & (uint32_t)(t->capacity - 1);
    for (;;) {
        Entry *e = &t->entries[index];
        if (e->key == NULL) {
            if (IS_NIL(e->value)) return NULL; // genuine empty
        } else if (e->key->length == length &&
                   e->key->hash   == hash   &&
                   memcmp(e->key->chars, chars, (size_t)length) == 0) {
            return e->key;
        }
        index = (index + 1) & (uint32_t)(t->capacity - 1);
    }
}
