#pragma once

#include <fir/dbg_info.h>

#include "token.h"

struct lexer {
    char* data;
    size_t bytes_left;
    struct fir_source_pos source_pos;
};

struct lexer lexer_create(char* data, size_t size);
struct token lexer_advance(struct lexer*);
