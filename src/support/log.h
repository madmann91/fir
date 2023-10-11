#pragma once

#include "fir/dbg_info.h"

#include "str.h"

#include <stddef.h>
#include <stdio.h>

enum msg_tag {
    MSG_ERR,
    MSG_WARN,
    MSG_NOTE
};

struct log {
    FILE* file;
    bool disable_colors;
    size_t max_errors;
    size_t error_count;
    const char* source_name;
    struct str_view source_data;
};

void log_msg(enum msg_tag, struct log*, const struct fir_source_range*, const char*, va_list);

[[gnu::format(printf, 3, 4)]]
void log_error(struct log*, const struct fir_source_range*, const char* fmt, ...);
[[gnu::format(printf, 3, 4)]]
void log_warn(struct log*, const struct fir_source_range*, const char* fmt, ...);
[[gnu::format(printf, 3, 4)]]
void log_note(struct log*, const struct fir_source_range*, const char* fmt, ...);
