#pragma once
#include "common.h"
#include "value.h"

typedef enum {
    // Constants / literals
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,

    // Stack
    OP_POP,

    // Variables — global
    OP_DEFINE_GLOBAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,

    // Variables — local
    OP_GET_LOCAL,
    OP_SET_LOCAL,

    // Upvalues / closures
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_CLOSE_UPVALUE,

    // Properties
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_GET_SUPER,

    // Arithmetic
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NEGATE,

    // Comparison
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,

    // Logic
    OP_NOT,

    // I/O
    OP_PRINT,

    // Control flow
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,

    // Functions
    OP_CALL,
    OP_CLOSURE,
    OP_RETURN,

    // Classes
    OP_CLASS,
    OP_INHERIT,
    OP_METHOD,
    OP_INVOKE,
    OP_SUPER_INVOKE,
} OpCode;

typedef struct {
    int      count;
    int      capacity;
    uint8_t *code;
    int     *lines;    // one entry per instruction (same length as code)
    ValueArray constants;
} Chunk;

void chunk_init(Chunk *chunk);
void chunk_free(Chunk *chunk);
void chunk_write(Chunk *chunk, uint8_t byte, int line);
int  chunk_add_constant(Chunk *chunk, Value value);
