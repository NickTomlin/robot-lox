#pragma once
#include "common.h"
#include "object.h"

void gc_collect(void);
void gc_mark_value(Value v);
void gc_mark_object(Obj *obj);
