#include "vm.h"
#include "compiler.h"
#include "object.h"
#include "memory.h"
#include "gc.h"
#include <time.h>
#include <stdarg.h>

VM vm;

// ============================================================
// Stack operations
// ============================================================

void vm_push(Value v) {
    *vm.stack_top++ = v;
}

Value vm_pop(void) {
    return *--vm.stack_top;
}

Value vm_peek(int distance) {
    return vm.stack_top[-1 - distance];
}

// ============================================================
// Native functions
// ============================================================

static Value native_clock(int argc, Value *args) {
    (void)argc; (void)args;
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void define_native(const char *name, NativeFn fn) {
    vm_push(OBJ_VAL(obj_string_copy(name, (int)strlen(name))));
    vm_push(OBJ_VAL(obj_native_new(fn)));
    table_set(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    vm_pop();
    vm_pop();
}

// ============================================================
// Runtime error
// ============================================================

static void runtime_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputs("\n", stderr);

    // Stack trace
    for (int i = vm.frame_count - 1; i >= 0; i--) {
        CallFrame *frame = &vm.frames[i];
        ObjFunction *fn  = frame->closure->function;
        size_t instruction = (size_t)(frame->ip - fn->chunk.code - 1);
        fprintf(stderr, "[line %d] in ", fn->chunk.lines[instruction]);
        if (fn->name == NULL) fprintf(stderr, "script\n");
        else                  fprintf(stderr, "%s()\n", fn->name->chars);
    }

    vm.stack_top   = vm.stack;
    vm.frame_count = 0;
}

// ============================================================
// Call helpers
// ============================================================

static bool call_closure(ObjClosure *cl, int argc) {
    if (argc != cl->function->arity) {
        runtime_error("Expected %d arguments but got %d.",
                      cl->function->arity, argc);
        return false;
    }
    if (vm.frame_count == FRAMES_MAX) {
        runtime_error("Stack overflow.");
        return false;
    }
    CallFrame *frame = &vm.frames[vm.frame_count++];
    frame->closure   = cl;
    frame->ip        = cl->function->chunk.code;
    frame->slots     = vm.stack_top - argc - 1;
    return true;
}

static bool call_value(Value callee, int argc) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE:
                return call_closure(AS_CLOSURE(callee), argc);
            case OBJ_NATIVE: {
                NativeFn fn = AS_NATIVE(callee);
                Value result = fn(argc, vm.stack_top - argc);
                vm.stack_top -= argc + 1;
                vm_push(result);
                return true;
            }
            case OBJ_CLASS: {
                ObjClass *klass = AS_CLASS(callee);
                vm.stack_top[-argc - 1] = OBJ_VAL(obj_instance_new(klass));
                Value initializer;
                if (table_get(&klass->methods, vm.init_string, &initializer)) {
                    return call_closure(AS_CLOSURE(initializer), argc);
                } else if (argc != 0) {
                    runtime_error("Expected 0 arguments but got %d.", argc);
                    return false;
                }
                return true;
            }
            case OBJ_BOUND_METHOD: {
                ObjBoundMethod *bm = AS_BOUND_METHOD(callee);
                vm.stack_top[-argc - 1] = bm->receiver;
                return call_closure(bm->method, argc);
            }
            default: break;
        }
    }
    runtime_error("Can only call functions and classes.");
    return false;
}

static bool invoke_from_class(ObjClass *klass, ObjString *name, int argc) {
    Value method;
    if (!table_get(&klass->methods, name, &method)) {
        runtime_error("Undefined property '%s'.", name->chars);
        return false;
    }
    return call_closure(AS_CLOSURE(method), argc);
}

static bool invoke(ObjString *name, int argc) {
    Value receiver = vm_peek(argc);
    if (!IS_INSTANCE(receiver)) {
        runtime_error("Only instances have methods.");
        return false;
    }
    ObjInstance *inst = AS_INSTANCE(receiver);

    // Check fields first (a field could shadow a method)
    Value val;
    if (table_get(&inst->fields, name, &val)) {
        vm.stack_top[-argc - 1] = val;
        return call_value(val, argc);
    }
    return invoke_from_class(inst->klass, name, argc);
}

static bool bind_method(ObjClass *klass, ObjString *name) {
    Value method;
    if (!table_get(&klass->methods, name, &method)) {
        runtime_error("Undefined property '%s'.", name->chars);
        return false;
    }
    ObjBoundMethod *bm = obj_bound_method_new(vm_peek(0), AS_CLOSURE(method));
    vm_pop();
    vm_push(OBJ_VAL(bm));
    return true;
}

// ============================================================
// Upvalue helpers
// ============================================================

static ObjUpvalue *capture_upvalue(Value *local) {
    ObjUpvalue *prev = NULL;
    ObjUpvalue *uv   = vm.open_upvalues;
    while (uv != NULL && uv->location > local) {
        prev = uv;
        uv   = uv->next;
    }
    if (uv != NULL && uv->location == local) return uv;

    ObjUpvalue *new_uv = obj_upvalue_new(local);
    new_uv->next = uv;
    if (prev == NULL) vm.open_upvalues       = new_uv;
    else              prev->next             = new_uv;
    return new_uv;
}

static void close_upvalues(Value *last) {
    while (vm.open_upvalues != NULL && vm.open_upvalues->location >= last) {
        ObjUpvalue *uv = vm.open_upvalues;
        uv->closed     = *uv->location;
        uv->location   = &uv->closed;
        vm.open_upvalues = uv->next;
    }
}

// ============================================================
// VM init / free
// ============================================================

void vm_init(void) {
    vm.stack_top      = vm.stack;
    vm.frame_count    = 0;
    vm.objects        = NULL;
    vm.open_upvalues  = NULL;
    vm.bytes_allocated = 0;
    vm.next_gc        = 1024 * 1024; // 1MB initial threshold
    vm.gray_stack     = NULL;
    vm.gray_count     = 0;
    vm.gray_capacity  = 0;

    table_init(&vm.globals);
    table_init(&vm.strings);

    vm.init_string = NULL; // must be NULL before obj_string_copy (GC safety)
    vm.init_string = obj_string_copy("init", 4);

    define_native("clock", native_clock);
}

void vm_free(void) {
    table_free(&vm.globals);
    table_free(&vm.strings);
    vm.init_string = NULL;

    // Free all heap objects
    Obj *obj = vm.objects;
    while (obj != NULL) {
        Obj *next = obj->next;
        obj_free(obj);
        obj = next;
    }
    free(vm.gray_stack);
}

// ============================================================
// Main dispatch loop
// ============================================================

#define READ_BYTE()     (*frame->ip++)
#define READ_SHORT()    (frame->ip += 2, \
                         (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING()   AS_STRING(READ_CONSTANT())

#define BINARY_OP(value_type, op) \
    do { \
        if (!IS_NUMBER(vm_peek(0)) || !IS_NUMBER(vm_peek(1))) { \
            runtime_error("Operands must be numbers."); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        double b = AS_NUMBER(vm_pop()); \
        double a = AS_NUMBER(vm_pop()); \
        vm_push(value_type(a op b)); \
    } while (0)

static InterpretResult run(void) {
    CallFrame *frame = &vm.frames[vm.frame_count - 1];

    for (;;) {
#if DEBUG_TRACE_EXEC
        printf("          ");
        for (Value *v = vm.stack; v < vm.stack_top; v++) {
            printf("[ "); value_print(*v); printf(" ]");
        }
        printf("\n");
#endif

        uint8_t instruction = READ_BYTE();
        switch (instruction) {

            // ---- Constants ----
            case OP_CONSTANT: vm_push(READ_CONSTANT()); break;
            case OP_NIL:      vm_push(NIL_VAL);         break;
            case OP_TRUE:     vm_push(BOOL_VAL(true));  break;
            case OP_FALSE:    vm_push(BOOL_VAL(false)); break;

            // ---- Stack ----
            case OP_POP: vm_pop(); break;

            // ---- Globals ----
            case OP_DEFINE_GLOBAL: {
                ObjString *name = READ_STRING();
                table_set(&vm.globals, name, vm_peek(0));
                vm_pop();
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString *name = READ_STRING();
                Value val;
                if (!table_get(&vm.globals, name, &val)) {
                    runtime_error("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm_push(val);
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString *name = READ_STRING();
                if (table_set(&vm.globals, name, vm_peek(0))) {
                    table_delete(&vm.globals, name);
                    runtime_error("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            // ---- Locals ----
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                vm_push(frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = vm_peek(0);
                break;
            }

            // ---- Upvalues ----
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                vm_push(*frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = vm_peek(0);
                break;
            }
            case OP_CLOSE_UPVALUE:
                close_upvalues(vm.stack_top - 1);
                vm_pop();
                break;

            // ---- Properties ----
            case OP_GET_PROPERTY: {
                if (!IS_INSTANCE(vm_peek(0))) {
                    runtime_error("Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjInstance *inst = AS_INSTANCE(vm_peek(0));
                ObjString   *name = READ_STRING();
                Value val;
                if (table_get(&inst->fields, name, &val)) {
                    vm_pop(); // instance
                    vm_push(val);
                    break;
                }
                if (!bind_method(inst->klass, name)) return INTERPRET_RUNTIME_ERROR;
                break;
            }
            case OP_SET_PROPERTY: {
                if (!IS_INSTANCE(vm_peek(1))) {
                    runtime_error("Only instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjInstance *inst = AS_INSTANCE(vm_peek(1));
                ObjString   *name = READ_STRING();
                table_set(&inst->fields, name, vm_peek(0));
                Value val = vm_pop();
                vm_pop(); // instance
                vm_push(val);
                break;
            }
            case OP_GET_SUPER: {
                ObjString *name  = READ_STRING();
                ObjClass  *super = AS_CLASS(vm_pop());
                if (!bind_method(super, name)) return INTERPRET_RUNTIME_ERROR;
                break;
            }

            // ---- Arithmetic ----
            case OP_ADD: {
                if (IS_STRING(vm_peek(0)) && IS_STRING(vm_peek(1))) {
                    ObjString *b = AS_STRING(vm_pop());
                    ObjString *a = AS_STRING(vm_pop());
                    int len      = a->length + b->length;
                    char *chars  = ALLOCATE(char, len + 1);
                    memcpy(chars, a->chars, (size_t)a->length);
                    memcpy(chars + a->length, b->chars, (size_t)b->length);
                    chars[len] = '\0';
                    ObjString *result = obj_string_take(chars, len);
                    vm_push(OBJ_VAL(result));
                } else if (IS_NUMBER(vm_peek(0)) && IS_NUMBER(vm_peek(1))) {
                    double b = AS_NUMBER(vm_pop());
                    double a = AS_NUMBER(vm_pop());
                    vm_push(NUMBER_VAL(a + b));
                } else {
                    runtime_error("Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
            case OP_NEGATE:
                if (!IS_NUMBER(vm_peek(0))) {
                    runtime_error("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm_push(NUMBER_VAL(-AS_NUMBER(vm_pop())));
                break;

            // ---- Comparison ----
            case OP_EQUAL: {
                Value b = vm_pop(), a = vm_pop();
                vm_push(BOOL_VAL(value_equals(a, b)));
                break;
            }
            case OP_NOT_EQUAL: {
                Value b = vm_pop(), a = vm_pop();
                vm_push(BOOL_VAL(!value_equals(a, b)));
                break;
            }
            case OP_GREATER:       BINARY_OP(BOOL_VAL, >);  break;
            case OP_GREATER_EQUAL: BINARY_OP(BOOL_VAL, >=); break;
            case OP_LESS:          BINARY_OP(BOOL_VAL, <);  break;
            case OP_LESS_EQUAL:    BINARY_OP(BOOL_VAL, <=); break;

            // ---- Logic ----
            case OP_NOT:
                vm_push(BOOL_VAL(!value_is_truthy(vm_pop())));
                break;

            // ---- I/O ----
            case OP_PRINT:
                value_print(vm_pop());
                printf("\n");
                break;

            // ---- Control flow ----
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (!value_is_truthy(vm_peek(0))) frame->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }

            // ---- Functions ----
            case OP_CALL: {
                int argc = READ_BYTE();
                if (!call_value(vm_peek(argc), argc)) return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frame_count - 1];
                break;
            }
            case OP_CLOSURE: {
                ObjFunction *fn = AS_FUNCTION(READ_CONSTANT());
                ObjClosure  *cl = obj_closure_new(fn);
                vm_push(OBJ_VAL(cl));
                for (int i = 0; i < cl->upvalue_count; i++) {
                    uint8_t is_local = READ_BYTE();
                    uint8_t index    = READ_BYTE();
                    if (is_local)
                        cl->upvalues[i] = capture_upvalue(frame->slots + index);
                    else
                        cl->upvalues[i] = frame->closure->upvalues[index];
                }
                break;
            }
            case OP_RETURN: {
                Value result = vm_pop();
                close_upvalues(frame->slots);
                vm.frame_count--;
                if (vm.frame_count == 0) {
                    vm_pop(); // pop <script>
                    return INTERPRET_OK;
                }
                vm.stack_top = frame->slots;
                vm_push(result);
                frame = &vm.frames[vm.frame_count - 1];
                break;
            }

            // ---- Classes ----
            case OP_CLASS: {
                ObjString *name = READ_STRING();
                vm_push(OBJ_VAL(obj_class_new(name)));
                break;
            }
            case OP_INHERIT: {
                Value superclass = vm_peek(1);
                if (!IS_CLASS(superclass)) {
                    runtime_error("Superclass must be a class.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjClass *subclass = AS_CLASS(vm_peek(0));
                table_copy(&AS_CLASS(superclass)->methods, &subclass->methods);
                vm_pop(); // subclass
                break;
            }
            case OP_METHOD: {
                ObjString *name = READ_STRING();
                Value method    = vm_peek(0);
                ObjClass *klass = AS_CLASS(vm_peek(1));
                table_set(&klass->methods, name, method);
                vm_pop();
                break;
            }
            case OP_INVOKE: {
                ObjString *method = READ_STRING();
                int argc          = READ_BYTE();
                if (!invoke(method, argc)) return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frame_count - 1];
                break;
            }
            case OP_SUPER_INVOKE: {
                ObjString *method = READ_STRING();
                int argc          = READ_BYTE();
                ObjClass  *super  = AS_CLASS(vm_pop());
                if (!invoke_from_class(super, method, argc)) return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frame_count - 1];
                break;
            }
        }
    }
}

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP

// ============================================================
// Public entry
// ============================================================

InterpretResult vm_interpret(const char *source) {
    ObjFunction *fn = compile(source);
    if (fn == NULL) return INTERPRET_COMPILE_ERROR;

    vm_push(OBJ_VAL(fn));
    ObjClosure *cl = obj_closure_new(fn);
    vm_pop();
    vm_push(OBJ_VAL(cl));
    call_closure(cl, 0);

    return run();
}
