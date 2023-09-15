#pragma once

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

[[noreturn]]
static inline void die(const char* msg) {
    fputs(msg, stderr);
    abort();
}

static inline void* xmalloc(size_t size) {
    void* p = malloc(size);
    if (!p)
        die("out of memory, malloc() failed.\n");
    return p;
}

static inline void* xcalloc(size_t count, size_t size) {
    void* p = calloc(count, size);
    if (!p)
        die("out of memory, malloc() failed.\n");
    return p;
}

static inline void* xrealloc(void* p, size_t size) {
    p = realloc(p, size);
    if (!p)
        die("out of memory, realloc() failed.\n");
    return p;
}
