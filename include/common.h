#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Flip to 1 to print disassembly and trace execution
#define DEBUG_PRINT_CODE  0
#define DEBUG_TRACE_EXEC  0
// Flip to 1 to stress-test the GC (collect on every allocation)
#define DEBUG_STRESS_GC   0
#define DEBUG_LOG_GC      0

#define UINT8_COUNT (UINT8_MAX + 1)
