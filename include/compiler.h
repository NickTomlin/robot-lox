#pragma once
#include "common.h"
#include "object.h"

// Compile source to a top-level ObjFunction.
// Returns NULL on compile error.
ObjFunction *compile(const char *source);

// Called by the GC to mark in-progress function objects
void compiler_mark_roots(void);
