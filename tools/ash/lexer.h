#pragma once

#include <fir/dbg_info.h>

#include "token.h"

struct lexer {
    const char* data;
    size_t bytes_left;
    struct fir_source_pos source_pos;
};

struct lexer lexer_create(const char* data, size_t size);
struct token lexer_advance(struct lexer*);
