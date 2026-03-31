#include "chunk.h"
#include "memory.h"

void chunk_init(Chunk *chunk) {
    chunk->count    = 0;
    chunk->capacity = 0;
    chunk->code     = NULL;
    chunk->lines    = NULL;
    value_array_init(&chunk->constants);
}

void chunk_free(Chunk *chunk) {
    reallocate(chunk->code,  (size_t)chunk->capacity * sizeof(uint8_t), 0);
    reallocate(chunk->lines, (size_t)chunk->capacity * sizeof(int),     0);
    value_array_free(&chunk->constants);
    chunk_init(chunk);
}

void chunk_write(Chunk *chunk, uint8_t byte, int line) {
    if (chunk->count >= chunk->capacity) {
        int old       = chunk->capacity;
        chunk->capacity = old < 8 ? 8 : old * 2;
        chunk->code  = (uint8_t *)reallocate(chunk->code,
                                              (size_t)old * sizeof(uint8_t),
                                              (size_t)chunk->capacity * sizeof(uint8_t));
        chunk->lines = (int *)reallocate(chunk->lines,
                                          (size_t)old * sizeof(int),
                                          (size_t)chunk->capacity * sizeof(int));
    }
    chunk->code[chunk->count]  = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

int chunk_add_constant(Chunk *chunk, Value value) {
    value_array_write(&chunk->constants, value);
    return chunk->constants.count - 1;
}
