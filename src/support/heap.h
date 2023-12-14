#pragma once

#include <stdbool.h>
#include <stddef.h>

void heap_push(
    void* begin,
    size_t count,
    size_t size,
    const void* elem,
    bool (*less_than)(const void*, const void*));

void heap_pop(
    void* begin,
    size_t count,
    size_t size,
    bool (*less_than)(const void*, const void*));
