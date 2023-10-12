#pragma once

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>

struct format_out;

struct format_buf {
    char* data;
    size_t size;
    size_t capacity;
};

enum format_out_tag {
    FORMAT_OUT_FILE,
    FORMAT_OUT_BUF
};

struct format_out {
    enum format_out_tag tag;
    union {
        FILE* file;
        struct format_buf* format_buf;
    };
};

[[gnu::format(printf, 2, 3)]]
void format(struct format_out*, const char* fmt, ...);
