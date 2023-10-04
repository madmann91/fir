#include "lexer.h"

#include "support/mem_pool.h"
#include "support/io.h"
#include "support/log.h"

#include <assert.h>

#define TOKEN_LOOKAHEAD 4

struct parser {
    struct lexer lexer;
    struct mem_pool* mem_pool;
    struct log* log;
    struct token ahead[TOKEN_LOOKAHEAD];
};

static inline void next_token(struct parser* parser) {
    for (size_t i = 1; i < TOKEN_LOOKAHEAD; ++i)
        parser->ahead[i - 1] = parser->ahead[i];
    parser->ahead[TOKEN_LOOKAHEAD - 1] = lexer_advance(&parser->lexer);
}

static inline void eat_token(struct parser* parser, [[maybe_unused]] enum token_tag tag) {
    assert(parser->ahead[0].tag == tag);
    next_token(parser);
}

static inline bool accept_token(struct parser* parser, enum token_tag tag) {
    if (parser->ahead->tag == tag) {
        next_token(parser);
        return true;
    }
    return false;
}

static inline bool expect_token(struct parser* parser, enum token_tag tag) {
    if (!accept_token(parser, tag)) {
        struct str_view str = token_str(parser->lexer.data, parser->ahead);
        log_error(parser->log,
            &parser->ahead->source_range,
            "expected '%s', but got '%.*s'",
            token_tag_to_string(tag),
            (int)str.length, str.data);
        return false;
    }
    return true;
}

static struct ast* parse_program(struct parser*) {
    return NULL;
}

struct ast* parse_file(
    const char* file_data,
    size_t file_size,
    struct mem_pool* mem_pool,
    struct log* log)
{
    struct parser parser = {
        .lexer = lexer_create(file_data, file_size),
        .mem_pool = mem_pool,
        .log = log,
    };
    return parse_program(&parser);
}
