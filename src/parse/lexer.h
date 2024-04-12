#pragma once

#include "token.h"

struct lexer {
    const char* data;
    size_t bytes_left;
    struct source_pos source_pos;
};

struct lexer lexer_create(const char* data, size_t size);
struct token lexer_advance(struct lexer*);
