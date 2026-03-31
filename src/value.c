#include "value.h"
#include "object.h"
#include "memory.h"

void value_array_init(ValueArray *arr) {
    arr->count    = 0;
    arr->capacity = 0;
    arr->values   = NULL;
}

void value_array_write(ValueArray *arr, Value v) {
    if (arr->count >= arr->capacity) {
        int old = arr->capacity;
        arr->capacity = GROW_CAPACITY(old);
        arr->values   = (Value *)reallocate(arr->values,
                                            (size_t)old * sizeof(Value),
                                            (size_t)arr->capacity * sizeof(Value));
    }
    arr->values[arr->count++] = v;
}

void value_array_free(ValueArray *arr) {
    reallocate(arr->values, (size_t)arr->capacity * sizeof(Value), 0);
    value_array_init(arr);
}

bool value_equals(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_NIL:    return true;
        case VAL_BOOL:   return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
        case VAL_OBJ:    return AS_OBJ(a) == AS_OBJ(b); // strings are interned
    }
    return false;
}

bool value_is_truthy(Value v) {
    return !IS_NIL(v) && !(IS_BOOL(v) && !AS_BOOL(v));
}

void value_print(Value v) {
    switch (v.type) {
        case VAL_NIL:    printf("nil"); break;
        case VAL_BOOL:   printf(AS_BOOL(v) ? "true" : "false"); break;
        case VAL_NUMBER: {
            double n = AS_NUMBER(v);
            // Print integers without decimal point
            if (n == (long long)n && n >= -1e15 && n <= 1e15)
                printf("%lld", (long long)n);
            else
                printf("%g", n);
            break;
        }
        case VAL_OBJ: obj_print(AS_OBJ(v)); break;
    }
}
