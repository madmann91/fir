#pragma once

#include "fir/dbg_info.h"

#include "str.h"

#include <stddef.h>
#include <stdio.h>

struct log;

struct log* log_create(FILE*, bool disable_colors, size_t max_errors);
void log_destroy(struct log*);

void log_reset(struct log*);
void log_set_source(struct log*, const char* source_name, struct str_view source_data);

size_t log_error_count(const struct log*);
void log_error(struct log*, const struct fir_source_range*, const char* fmt, ...);
void log_warn(struct log*, const struct fir_source_range*, const char* fmt, ...);
void log_note(struct log*, const struct fir_source_range*, const char* fmt, ...);
