#pragma once

#include "scanner.h"

#include <stdarg.h>

#define MAX_AHEAD 4

struct parser {
    struct scanner scanner;
    struct token ahead[MAX_AHEAD];
    void* error_data;
    void (*error_func) (void*, const struct fir_source_range*, const char* fmt, va_list);
};

struct parser parser_create(const char* data, size_t size);

void parser_error(
    struct parser*,
    const struct fir_source_range*,
    const char* fmt, ...);

void parser_next(struct parser* parser);
void parser_eat(struct parser*, enum token_tag);
bool parser_accept(struct parser*, enum token_tag);
bool parser_expect(struct parser*, enum token_tag);
