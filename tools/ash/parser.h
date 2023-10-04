#pragma once

#include <stddef.h>

struct mem_pool;
struct log;

struct ast* parse_file(
    const char* file_data,
    size_t file_size,
    struct mem_pool*,
    struct log*);
