#include "gc.h"
#include "memory.h"
#include "compiler.h"
#include "vm.h"
#include "table.h"

#define GC_HEAP_GROW_FACTOR 2

// ---- Marking ----

void gc_mark_object(Obj *obj) {
    if (obj == NULL) return;
    if (obj->is_marked) return;

#if DEBUG_LOG_GC
    printf("%p mark ", (void *)obj);
    value_print(OBJ_VAL(obj));
    printf("\n");
#endif

    obj->is_marked = true;

    // Add to gray worklist
    if (vm.gray_count >= vm.gray_capacity) {
        vm.gray_capacity = vm.gray_capacity < 8 ? 8 : vm.gray_capacity * 2;
        vm.gray_stack = (Obj **)realloc(vm.gray_stack,
                                         sizeof(Obj *) * (size_t)vm.gray_capacity);
        if (vm.gray_stack == NULL) { fprintf(stderr, "OOM in GC.\n"); exit(1); }
    }
    vm.gray_stack[vm.gray_count++] = obj;
}

void gc_mark_value(Value v) {
    if (IS_OBJ(v)) gc_mark_object(AS_OBJ(v));
}

static void mark_value_array(ValueArray *arr) {
    for (int i = 0; i < arr->count; i++) gc_mark_value(arr->values[i]);
}

static void blacken_object(Obj *obj) {
#if DEBUG_LOG_GC
    printf("%p blacken ", (void *)obj);
    value_print(OBJ_VAL(obj));
    printf("\n");
#endif
    switch (obj->type) {
        case OBJ_UPVALUE:
            gc_mark_value(((ObjUpvalue *)obj)->closed);
            break;
        case OBJ_FUNCTION: {
            ObjFunction *fn = (ObjFunction *)obj;
            gc_mark_object((Obj *)fn->name);
            mark_value_array(&fn->chunk.constants);
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure *cl = (ObjClosure *)obj;
            gc_mark_object((Obj *)cl->function);
            for (int i = 0; i < cl->upvalue_count; i++)
                gc_mark_object((Obj *)cl->upvalues[i]);
            break;
        }
        case OBJ_CLASS: {
            ObjClass *klass = (ObjClass *)obj;
            gc_mark_object((Obj *)klass->name);
            for (int i = 0; i < klass->methods.capacity; i++) {
                Entry *e = &klass->methods.entries[i];
                if (e->key != NULL) {
                    gc_mark_object((Obj *)e->key);
                    gc_mark_value(e->value);
                }
            }
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance *inst = (ObjInstance *)obj;
            gc_mark_object((Obj *)inst->klass);
            for (int i = 0; i < inst->fields.capacity; i++) {
                Entry *e = &inst->fields.entries[i];
                if (e->key != NULL) {
                    gc_mark_object((Obj *)e->key);
                    gc_mark_value(e->value);
                }
            }
            break;
        }
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod *bm = (ObjBoundMethod *)obj;
            gc_mark_value(bm->receiver);
            gc_mark_object((Obj *)bm->method);
            break;
        }
        case OBJ_NATIVE:
        case OBJ_STRING:
            break; // no children
    }
}

// ---- Mark roots ----

static void mark_roots(void) {
    // VM value stack
    for (Value *v = vm.stack; v < vm.stack_top; v++) gc_mark_value(*v);

    // Call frames (closures)
    for (int i = 0; i < vm.frame_count; i++)
        gc_mark_object((Obj *)vm.frames[i].closure);

    // Open upvalues
    for (ObjUpvalue *uv = vm.open_upvalues; uv != NULL; uv = uv->next)
        gc_mark_object((Obj *)uv);

    // Globals
    for (int i = 0; i < vm.globals.capacity; i++) {
        Entry *e = &vm.globals.entries[i];
        if (e->key != NULL) {
            gc_mark_object((Obj *)e->key);
            gc_mark_value(e->value);
        }
    }

    gc_mark_object((Obj *)vm.init_string);

    // Compiler roots (in-progress functions)
    compiler_mark_roots();
}

// ---- Trace gray ----

static void trace_references(void) {
    while (vm.gray_count > 0) {
        Obj *obj = vm.gray_stack[--vm.gray_count];
        blacken_object(obj);
    }
}

// ---- Sweep ----

static void sweep(void) {
    Obj *prev = NULL;
    Obj *obj  = vm.objects;
    while (obj != NULL) {
        if (obj->is_marked) {
            obj->is_marked = false;
            prev = obj;
            obj  = obj->next;
        } else {
            Obj *unreached = obj;
            obj = obj->next;
            if (prev != NULL) prev->next = obj;
            else              vm.objects  = obj;
            obj_free(unreached);
        }
    }
}

static void remove_white_strings(void) {
    // Weak references — remove interned strings that weren't marked
    for (int i = 0; i < vm.strings.capacity; i++) {
        Entry *e = &vm.strings.entries[i];
        if (e->key != NULL && !e->key->obj.is_marked) {
            table_delete(&vm.strings, e->key);
        }
    }
}

void gc_collect(void) {
#if DEBUG_LOG_GC
    printf("-- gc begin (allocated %zu)\n", vm.bytes_allocated);
#endif
    mark_roots();
    trace_references();
    remove_white_strings();
    sweep();
    vm.next_gc = vm.bytes_allocated * GC_HEAP_GROW_FACTOR;
    if (vm.next_gc < 1024 * 1024) vm.next_gc = 1024 * 1024; // min 1MB

#if DEBUG_LOG_GC
    printf("-- gc end   (allocated %zu)\n", vm.bytes_allocated);
#endif
}
