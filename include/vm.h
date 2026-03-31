#pragma once
#include "common.h"
#include "chunk.h"
#include "value.h"
#include "table.h"
#include "object.h"

#define FRAMES_MAX 64
#define STACK_MAX  (FRAMES_MAX * UINT8_COUNT)

typedef struct {
    ObjClosure *closure;
    uint8_t    *ip;
    Value      *slots;  // base of this frame's window in the stack
} CallFrame;

typedef struct {
    CallFrame  frames[FRAMES_MAX];
    int        frame_count;

    Value      stack[STACK_MAX];
    Value     *stack_top;

    Table      globals;
    Table      strings;   // interned string set

    ObjString *init_string;

    ObjUpvalue *open_upvalues;

    // GC bookkeeping
    Obj       *objects;
    size_t     bytes_allocated;
    size_t     next_gc;
    Obj      **gray_stack;
    int        gray_count;
    int        gray_capacity;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

// Global VM instance (defined in vm.c, used by object.c)
extern VM vm;

void             vm_init(void);
void             vm_free(void);
InterpretResult  vm_interpret(const char *source);

// Stack helpers used by object.c during allocation
void  vm_push(Value v);
Value vm_pop(void);
Value vm_peek(int distance);
