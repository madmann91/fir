#pragma once

#include <stddef.h>
#include <stdio.h>

static inline void print_indent(size_t indent) {
    for (size_t i = 0; i < indent; ++i)
        printf("    ");
}
