#include "lexer.h"
#include "ast.h"

#include "support/mem_pool.h"
#include "support/io.h"
#include "support/log.h"

#include <assert.h>
#include <string.h>

#define TOKEN_LOOKAHEAD 4

struct parser {
    struct lexer lexer;
    struct mem_pool* mem_pool;
    struct log* log;
    struct token ahead[TOKEN_LOOKAHEAD];
    struct fir_source_pos prev_pos;
};

static struct ast* parse_type(struct parser*);
static struct ast* parse_pattern(struct parser*);
static struct ast* parse_expr(struct parser*);
static struct ast* parse_decl(struct parser*);

static inline void next_token(struct parser* parser) {
    for (size_t i = 1; i < TOKEN_LOOKAHEAD; ++i)
        parser->ahead[i - 1] = parser->ahead[i];
    parser->prev_pos = parser->ahead->source_range.end;
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

static inline struct ast* alloc_ast(
    struct parser* parser,
    const struct fir_source_pos* begin_pos,
    const struct ast* ast)
{
    struct ast* copy = MEM_POOL_ALLOC(*parser->mem_pool, struct ast);
    memcpy(copy, ast, sizeof(struct ast));
    copy->source_range.begin = *begin_pos;
    copy->source_range.end = parser->prev_pos;
    return copy;
}

static const char* parse_ident(struct parser* parser) {
    struct str_view str = token_str(parser->lexer.data, parser->ahead);
    char* ident = mem_pool_alloc(parser->mem_pool, sizeof(char) * (str.length + 1), alignof(char));
    memcpy(ident, str.data, str.length);
    ident[str.length] = 0;
    expect_token(parser, TOK_IDENT);
    return ident;
}

static struct ast* parse_many(
    struct parser* parser,
    enum token_tag stop,
    enum token_tag sep,
    struct ast* (parse_func)(struct parser*))
{
    struct ast* first = NULL;
    struct ast** prev = &first;
    while (parser->ahead->tag != stop) {
        *prev = parse_func(parser);
        prev = &(*prev)->next;
        if (sep != TOK_ERR && !accept_token(parser, sep))
            break;
    }
    expect_token(parser, stop);
    return first;
}

static struct ast* parse_error(struct parser* parser, const char* msg) {
    struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    struct str_view str = token_str(parser->lexer.data, parser->ahead);
    log_error(parser->log,
        &parser->ahead->source_range,
        "expected %s, but got '%.*s'",
        msg, (int)str.length, str.data);
    next_token(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) { .tag = AST_ERROR });
}

static struct ast* parse_prim_type(struct parser* parser, enum prim_type_tag tag) {
    struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, (enum token_tag)tag);
    return alloc_ast(parser, &begin_pos, &(struct ast) { .tag = AST_PRIM_TYPE, .prim_type.tag = tag });
}

static struct ast* parse_type(struct parser* parser) {
    switch (parser->ahead->tag) {
#define x(tag, ...) case TOK_##tag: return parse_prim_type(parser, PRIM_TYPE_##tag);
        PRIM_TYPE_LIST(x)
#undef x
        default: return parse_error(parser, "type");
    }
}

static struct ast* parse_ident_pattern(struct parser* parser) {
    struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    const char* name = parse_ident(parser);
    struct ast* type = NULL;
    if (accept_token(parser, TOK_COLON))
        type = parse_type(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_IDENT_PATTERN,
        .ident_pattern = {
            .name = name,
            .type = type
        }
    });
}

static struct ast* parse_pattern(struct parser* parser) {
    switch (parser->ahead->tag) {
        case TOK_IDENT: return parse_ident_pattern(parser);
        default:        return parse_error(parser, "pattern");
    }
}

static struct ast* parse_func_decl(struct parser* parser) {
    struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOK_FUNC);
    const char* name = parse_ident(parser);
    expect_token(parser, TOK_LPAREN);
    struct ast* param = parse_pattern(parser);
    expect_token(parser, TOK_RPAREN);
    struct ast* ret_type = NULL;
    if (accept_token(parser, TOK_THIN_ARROW))
        ret_type = parse_type(parser);
    struct ast* body = NULL;
    if (accept_token(parser, TOK_EQ))
        body = parse_expr(parser);
    expect_token(parser, TOK_SEMICOLON);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_FUNC_DECL,
        .func_decl = {
            .name = name,
            .param = param,
            .ret_type = ret_type,
            .body = body
        }
    });
}

static struct ast* parse_const_decl(struct parser* parser) {
    struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOK_CONST);
    struct ast* pattern = parse_pattern(parser);
    expect_token(parser, TOK_EQ);
    struct ast* init = parse_expr(parser);
    expect_token(parser, TOK_SEMICOLON);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_CONST_DECL,
        .const_decl = {
            .pattern = pattern,
            .init = init
        }
    });
}

static struct ast* parse_decl(struct parser* parser) {
    switch (parser->ahead->tag) {
        case TOK_FUNC:  return parse_func_decl(parser);
        case TOK_CONST: return parse_const_decl(parser);
        default:        return parse_error(parser, "declaration");
    }
}

static struct ast* parse_ident_expr(struct parser* parser) {
    struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    const char* name = parse_ident(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_IDENT_EXPR,
        .ident_pattern.name = name
    });
}

static struct ast* parse_expr(struct parser* parser) {
    switch (parser->ahead->tag) {
        case TOK_IDENT: return parse_ident_expr(parser);
        default:        return parse_error(parser, "expression");
    }
}

static struct ast* parse_program(struct parser* parser) {
    const struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    struct ast* decls = parse_many(parser, TOK_EOF, TOK_ERR, parse_decl);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_PROGRAM,
        .program.decls = decls
    });
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
    for (size_t i = 0; i < TOKEN_LOOKAHEAD; ++i)
        next_token(&parser);
    return parse_program(&parser);
}
