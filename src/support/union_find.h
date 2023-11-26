#pragma once

#include <stddef.h>

size_t union_find(size_t* parents, size_t);
void union_merge(size_t* parents, size_t, size_t);
