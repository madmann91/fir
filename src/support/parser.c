#include "parser.h"

struct parser parser_create(const char* data, size_t size) {
    struct parser parser = {
        .scanner = scanner_create(data, size),
    };
    for (size_t i = 0; i < MAX_AHEAD; ++i)
        parser_next(&parser);
    return parser;
}

void parser_next(struct parser* parser) {
    for (size_t i = 1; i < MAX_AHEAD; ++i)
        parser->ahead[i - 1] = parser->ahead[i];
    parser->ahead[MAX_AHEAD - 1] = scanner_advance(&parser->scanner);
}

void parser_error(
    struct parser* parser,
    const struct fir_source_range* source_range,
    const char* fmt, ...)
{
    if (!parser->error_func)
        return;
    va_list args;
    va_start(args, fmt);
    parser->error_func(parser->error_data, source_range, fmt, args);
    va_end(args);
}

void parser_eat(struct parser* parser, [[maybe_unused]] enum token_tag tag) {
    assert(parser->ahead[0].tag == tag);
    parser_next(parser);
}

bool parser_accept(struct parser* parser, enum token_tag tag) {
    if (parser->ahead->tag == tag) {
        parser_next(parser);
        return true;
    }
    return false;
}

bool parser_expect(struct parser* parser, enum token_tag tag) {
    if (!parser_accept(parser, tag)) {
        struct str_view str = token_str(parser->scanner.data, parser->ahead);
        parser_error(parser,
            &parser->ahead->source_range,
            "expected '%s', but got '%.*s'",
            token_tag_to_string(tag),
            (int)str.length, str.data);
        return false;
    }
    return true;
}
