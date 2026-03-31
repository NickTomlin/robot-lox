#pragma once
#include "common.h"

// Forward declaration — Obj is defined in object.h
typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_NUMBER,
    VAL_OBJ,
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool   boolean;
        double number;
        Obj   *obj;
    } as;
} Value;

// Constructors
#define NIL_VAL           ((Value){VAL_NIL,    {.number = 0}})
#define BOOL_VAL(b)       ((Value){VAL_BOOL,   {.boolean = (b)}})
#define NUMBER_VAL(n)     ((Value){VAL_NUMBER,  {.number  = (n)}})
#define OBJ_VAL(o)        ((Value){VAL_OBJ,    {.obj = (Obj*)(o)}})

// Type checks
#define IS_NIL(v)         ((v).type == VAL_NIL)
#define IS_BOOL(v)        ((v).type == VAL_BOOL)
#define IS_NUMBER(v)      ((v).type == VAL_NUMBER)
#define IS_OBJ(v)         ((v).type == VAL_OBJ)

// Unwrap
#define AS_BOOL(v)        ((v).as.boolean)
#define AS_NUMBER(v)      ((v).as.number)
#define AS_OBJ(v)         ((v).as.obj)

typedef struct {
    int    count;
    int    capacity;
    Value *values;
} ValueArray;

void value_array_init(ValueArray *arr);
void value_array_write(ValueArray *arr, Value v);
void value_array_free(ValueArray *arr);

bool  value_equals(Value a, Value b);
bool  value_is_truthy(Value v);
void  value_print(Value v);
