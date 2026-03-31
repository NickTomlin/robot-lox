#pragma once
#include "common.h"

// All heap allocations route through here so the GC can track bytes.
void *reallocate(void *ptr, size_t old_size, size_t new_size);

// Convenience macros
#define ALLOCATE(type, count) \
    (type *)reallocate(NULL, 0, sizeof(type) * (size_t)(count))

#define FREE(type, ptr) \
    reallocate((ptr), sizeof(type), 0)

#define FREE_ARRAY(type, ptr, count) \
    reallocate((ptr), sizeof(type) * (size_t)(count), 0)
