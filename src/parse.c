#include "fir/module.h"

#include "support/parser.h"

#include <stdio.h>

struct parse_context {
    const char* file_name;
    struct parser parser;
};

static void parse_error(
    void* error_data,
    const struct fir_source_range* source_range,
    const char* fmt,
    va_list args)
{
    struct parse_context* context = (struct parse_context*)error_data;
    fprintf(stderr, "error in %s(%d:%d - %d:%d): ",
        context->file_name,
        source_range->begin.row,
        source_range->begin.col,
        source_range->end.row,
        source_range->end.col);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
}

static inline const struct fir_node* parse_ty(struct parse_context* context) {
}

static inline struct str_view parse_ident(struct parse_context* context) {
    struct str_view ident = context->parser.ahead->str;
    parser_expect(&context->parser, TOK_IDENT);
    return ident;
}

static inline enum fir_node_tag parse_node_tag(struct parse_context* context) {
}

static inline union fir_node_data parse_node_data(struct parse_context* context) {
}

static inline const struct fir_node* parse_node(struct parse_context* context) {
    const struct fir_node* ty = parse_ty(context);
    struct str_view ident = parse_ident(context);
    parser_expect(&context->parser, TOK_EQ);
    enum fir_node_tag tag = parse_node_tag(context);
    union fir_node_data node_data;
    if (parser_accept(&context->parser, TOK_LBRACKET)) {
        node_data = parse_node_data(context);
        parser_expect(&context->parser, TOK_RBRACKET);
    }
    if (parser_accept(&context->parser, TOK_LPAREN)) {

        parser_expect(&context->parser, TOK_RPAREN);
    }
}

bool fir_parse_mod(
    const char* file_name,
    const char* file_data,
    size_t file_size,
    struct fir_mod* mod)
{
    struct parse_context context = {
        .file_name = file_name,
        .parser = parser_init(file_data, file_size)
    };
    context.parser.error_data = &context;
    context.parser.error_func = parse_error;

    while (context.parser.ahead->tag != TOK_EOF) {
        parse_node(&context);
    }

    return false;
}
