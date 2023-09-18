#pragma once

#include <stddef.h>

void* xmalloc(size_t);
void* xcalloc(size_t, size_t);
void* xrealloc(void*, size_t);
