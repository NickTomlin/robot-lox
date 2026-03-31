#include "object.h"
#include "memory.h"
#include "table.h"
#include "vm.h"

// Allocate and register a new object in the VM's GC list
static Obj *alloc_object(size_t size, ObjType type) {
    Obj *obj = (Obj *)reallocate(NULL, 0, size);
    obj->type      = type;
    obj->is_marked = false;
    obj->next      = vm.objects;
    vm.objects     = obj;

#if DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void *)obj, size, type);
#endif

    return obj;
}

#define ALLOC_OBJ(type, obj_type) \
    (type *)alloc_object(sizeof(type), obj_type)

// ---- FNV-1a hash ----
static uint32_t hash_string(const char *key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

static ObjString *allocate_string(char *chars, int length, uint32_t hash) {
    ObjString *s = ALLOC_OBJ(ObjString, OBJ_STRING);
    s->length = length;
    s->chars  = chars;
    s->hash   = hash;
    // Push onto stack to keep it alive during table_set (which may trigger GC)
    vm_push(OBJ_VAL(s));
    table_set(&vm.strings, s, NIL_VAL);
    vm_pop();
    return s;
}

ObjString *obj_string_copy(const char *chars, int length) {
    uint32_t hash     = hash_string(chars, length);
    ObjString *interned = table_find_string(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;

    char *heap = ALLOCATE(char, length + 1);
    memcpy(heap, chars, (size_t)length);
    heap[length] = '\0';
    return allocate_string(heap, length, hash);
}

ObjString *obj_string_take(char *chars, int length) {
    uint32_t hash     = hash_string(chars, length);
    ObjString *interned = table_find_string(&vm.strings, chars, length, hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }
    return allocate_string(chars, length, hash);
}

ObjFunction *obj_function_new(void) {
    ObjFunction *fn = ALLOC_OBJ(ObjFunction, OBJ_FUNCTION);
    fn->arity         = 0;
    fn->upvalue_count = 0;
    fn->name          = NULL;
    chunk_init(&fn->chunk);
    return fn;
}

ObjUpvalue *obj_upvalue_new(Value *slot) {
    ObjUpvalue *uv = ALLOC_OBJ(ObjUpvalue, OBJ_UPVALUE);
    uv->location = slot;
    uv->closed   = NIL_VAL;
    uv->next     = NULL;
    return uv;
}

ObjClosure *obj_closure_new(ObjFunction *fn) {
    ObjUpvalue **upvalues = ALLOCATE(ObjUpvalue *, fn->upvalue_count);
    for (int i = 0; i < fn->upvalue_count; i++) upvalues[i] = NULL;

    ObjClosure *cl = ALLOC_OBJ(ObjClosure, OBJ_CLOSURE);
    cl->function      = fn;
    cl->upvalues      = upvalues;
    cl->upvalue_count = fn->upvalue_count;
    return cl;
}

ObjNative *obj_native_new(NativeFn fn) {
    ObjNative *n = ALLOC_OBJ(ObjNative, OBJ_NATIVE);
    n->fn = fn;
    return n;
}

ObjClass *obj_class_new(ObjString *name) {
    ObjClass *klass = ALLOC_OBJ(ObjClass, OBJ_CLASS);
    klass->name = name;
    table_init(&klass->methods);
    return klass;
}

ObjInstance *obj_instance_new(ObjClass *klass) {
    ObjInstance *inst = ALLOC_OBJ(ObjInstance, OBJ_INSTANCE);
    inst->klass = klass;
    table_init(&inst->fields);
    return inst;
}

ObjBoundMethod *obj_bound_method_new(Value receiver, ObjClosure *method) {
    ObjBoundMethod *bm = ALLOC_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
    bm->receiver = receiver;
    bm->method   = method;
    return bm;
}

static void print_fn_name(ObjFunction *fn, const char *unnamed) {
    if (fn->name == NULL) printf("%s", unnamed);
    else printf("<fn %s>", fn->name->chars);
}

void obj_print(Obj *obj) {
    switch (obj->type) {
        case OBJ_STRING:
            printf("%s", ((ObjString *)obj)->chars);
            break;
        case OBJ_FUNCTION:
            print_fn_name((ObjFunction *)obj, "<script>");
            break;
        case OBJ_CLOSURE:
            print_fn_name(((ObjClosure *)obj)->function, "<script>");
            break;
        case OBJ_UPVALUE:
            printf("<upvalue>");
            break;
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
        case OBJ_CLASS:
            printf("%s", ((ObjClass *)obj)->name->chars);
            break;
        case OBJ_INSTANCE:
            printf("%s instance", ((ObjInstance *)obj)->klass->name->chars);
            break;
        case OBJ_BOUND_METHOD:
            print_fn_name(((ObjBoundMethod *)obj)->method->function, "<method>");
            break;
    }
}

void obj_free(Obj *obj) {
#if DEBUG_LOG_GC
    printf("%p free type %d\n", (void *)obj, obj->type);
#endif
    switch (obj->type) {
        case OBJ_STRING: {
            ObjString *s = (ObjString *)obj;
            FREE_ARRAY(char, s->chars, s->length + 1);
            FREE(ObjString, obj);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction *fn = (ObjFunction *)obj;
            chunk_free(&fn->chunk);
            FREE(ObjFunction, obj);
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure *cl = (ObjClosure *)obj;
            FREE_ARRAY(ObjUpvalue *, cl->upvalues, cl->upvalue_count);
            FREE(ObjClosure, obj);
            break;
        }
        case OBJ_UPVALUE:
            FREE(ObjUpvalue, obj);
            break;
        case OBJ_NATIVE:
            FREE(ObjNative, obj);
            break;
        case OBJ_CLASS: {
            ObjClass *klass = (ObjClass *)obj;
            table_free(&klass->methods);
            FREE(ObjClass, obj);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance *inst = (ObjInstance *)obj;
            table_free(&inst->fields);
            FREE(ObjInstance, obj);
            break;
        }
        case OBJ_BOUND_METHOD:
            FREE(ObjBoundMethod, obj);
            break;
    }
}
