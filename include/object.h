#pragma once
#include "common.h"
#include "chunk.h"
#include "value.h"

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_NATIVE,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_BOUND_METHOD,
} ObjType;

// Every heap object starts with this header
struct Obj {
    ObjType  type;
    bool     is_marked;
    struct Obj *next;  // intrusive linked list (GC uses this)
};

// ---- String ----
struct ObjString {
    Obj      obj;
    int      length;
    char    *chars;
    uint32_t hash;
};

// ---- Function ----
typedef struct {
    Obj        obj;
    int        arity;
    int        upvalue_count;
    Chunk      chunk;
    ObjString *name;
} ObjFunction;

// ---- Upvalue ----
typedef struct ObjUpvalue {
    Obj          obj;
    Value       *location;   // points to stack slot while open
    Value        closed;     // storage after closing
    struct ObjUpvalue *next; // linked list of open upvalues
} ObjUpvalue;

// ---- Closure ----
typedef struct {
    Obj          obj;
    ObjFunction *function;
    ObjUpvalue **upvalues;
    int          upvalue_count;
} ObjClosure;

// ---- Native function ----
typedef Value (*NativeFn)(int argc, Value *args);

typedef struct {
    Obj      obj;
    NativeFn fn;
} ObjNative;

// ---- Class ----
// Forward declaration needed by ObjInstance
typedef struct ObjClass ObjClass;

#include "table.h"

struct ObjClass {
    Obj        obj;
    ObjString *name;
    Table      methods;
};

// ---- Instance ----
typedef struct {
    Obj       obj;
    ObjClass *klass;
    Table     fields;
} ObjInstance;

// ---- Bound method ----
typedef struct {
    Obj        obj;
    Value      receiver;
    ObjClosure *method;
} ObjBoundMethod;

// Type checks
#define OBJ_TYPE(v)     (AS_OBJ(v)->type)
#define IS_STRING(v)    (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_STRING)
#define IS_FUNCTION(v)  (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_FUNCTION)
#define IS_CLOSURE(v)   (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_CLOSURE)
#define IS_NATIVE(v)    (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_NATIVE)
#define IS_CLASS(v)     (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_CLASS)
#define IS_INSTANCE(v)  (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_INSTANCE)
#define IS_BOUND_METHOD(v) (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_BOUND_METHOD)

// Unwrap
#define AS_STRING(v)       ((ObjString *)AS_OBJ(v))
#define AS_CSTRING(v)      (((ObjString *)AS_OBJ(v))->chars)
#define AS_FUNCTION(v)     ((ObjFunction *)AS_OBJ(v))
#define AS_CLOSURE(v)      ((ObjClosure *)AS_OBJ(v))
#define AS_NATIVE(v)       (((ObjNative *)AS_OBJ(v))->fn)
#define AS_CLASS(v)        ((ObjClass *)AS_OBJ(v))
#define AS_INSTANCE(v)     ((ObjInstance *)AS_OBJ(v))
#define AS_BOUND_METHOD(v) ((ObjBoundMethod *)AS_OBJ(v))

ObjString     *obj_string_copy(const char *chars, int length);
ObjString     *obj_string_take(char *chars, int length);
ObjFunction   *obj_function_new(void);
ObjUpvalue    *obj_upvalue_new(Value *slot);
ObjClosure    *obj_closure_new(ObjFunction *fn);
ObjNative     *obj_native_new(NativeFn fn);
ObjClass      *obj_class_new(ObjString *name);
ObjInstance   *obj_instance_new(ObjClass *klass);
ObjBoundMethod *obj_bound_method_new(Value receiver, ObjClosure *method);

void obj_print(Obj *obj);
void obj_free(Obj *obj);
