#include "memory.h"
#include "vm.h"
#include "gc.h"

#define GC_HEAP_GROW_FACTOR 2

void *reallocate(void *ptr, size_t old_size, size_t new_size) {
    vm.bytes_allocated += new_size;
    vm.bytes_allocated -= old_size;

#if DEBUG_STRESS_GC
    if (new_size > 0) gc_collect();
#else
    if (new_size > old_size && vm.bytes_allocated > vm.next_gc) {
        gc_collect();
    }
#endif

    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    void *result = realloc(ptr, new_size);
    if (result == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    return result;
}
