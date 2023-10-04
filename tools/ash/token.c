#include "token.h"

#include <assert.h>

const char* token_tag_to_string(enum token_tag tag) {
    switch (tag) {
#define x(tag, str) case TOK_##tag: return str;
        TOKEN_LIST(x)
#undef x
        default:
            assert(false && "invalid token");
            return NULL;
    }
}

bool token_tag_is_symbol(enum token_tag tag) {
    switch (tag) {
#define x(tag, ...) case TOK_##tag:
        SYMBOL_LIST(x)
#undef x
            return true;
        default:
            return false;
    }
}

bool token_tag_is_keyword(enum token_tag tag) {
    switch (tag) {
#define x(tag, ...) case TOK_##tag:
        KEYWORD_LIST(x)
#undef x
            return true;
        default:
            return false;
    }
}

struct str_view token_str(const char* data, const struct token* token) {
    return (struct str_view) {
        .data = data + token->source_range.begin.bytes,
        .length = token->source_range.end.bytes - token->source_range.end.bytes
    };
}

