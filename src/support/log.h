#pragma once

#include "fir/dbg_info.h"

#include "str.h"

#include <stddef.h>
#include <stdio.h>

struct log {
    FILE* file;
    bool disable_colors;
    size_t max_errors;
    size_t error_count;
    const char* source_name;
    struct str_view source_data;
};

void log_error(struct log*, const struct fir_source_range*, const char* fmt, ...);
void log_warn(struct log*, const struct fir_source_range*, const char* fmt, ...);
void log_note(struct log*, const struct fir_source_range*, const char* fmt, ...);
