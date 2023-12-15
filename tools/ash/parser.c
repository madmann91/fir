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
    struct fir_source_pos prev_end;
};

static struct ast* parse_type(struct parser*);
static struct ast* parse_pattern(struct parser*);
static struct ast* parse_expr(struct parser*);
static struct ast* parse_decl(struct parser*);

static inline void next_token(struct parser* parser) {
    parser->prev_end = parser->ahead->source_range.end;
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
        struct str_view str_view = token_str_view(parser->lexer.data, parser->ahead);
        log_error(parser->log,
            &parser->ahead->source_range,
            "expected '%s', but got '%.*s'",
            token_tag_to_string(tag),
            (int)str_view.length, str_view.data);
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
    copy->source_range.end = parser->prev_end;
    return copy;
}

static const char* parse_name(struct parser* parser) {
    struct str_view str_view = token_str_view(parser->lexer.data, parser->ahead);
    char* name = mem_pool_alloc(parser->mem_pool, sizeof(char) * (str_view.length + 1), alignof(char));
    memcpy(name, str_view.data, str_view.length);
    name[str_view.length] = 0;
    expect_token(parser, TOK_IDENT);
    return name;
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
    struct str_view str_view = token_str_view(parser->lexer.data, parser->ahead);
    log_error(parser->log,
        &parser->ahead->source_range,
        "expected %s, but got '%.*s'",
        msg, (int)str_view.length, str_view.data);
    next_token(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) { .tag = AST_ERROR });
}

static struct ast* parse_record(
    struct parser* parser,
    enum ast_tag tag,
    struct ast* (*parse_field)(struct parser*))
{
    struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOK_LBRACKET);
    struct ast* fields = parse_many(parser, TOK_RBRACKET, TOK_COMMA, parse_field);
    return alloc_ast(parser, &begin_pos, &(struct ast) { .tag = tag, .record_type.fields = fields });
}

static struct ast* parse_tuple(
    struct parser* parser,
    enum ast_tag tag,
    struct ast* (*parse_arg)(struct parser*))
{
    struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOK_LPAREN);
    struct ast* args = parse_many(parser, TOK_RPAREN, TOK_COMMA, parse_arg);
    if (args && !args->next)
        return args;
    return alloc_ast(parser, &begin_pos, &(struct ast) { .tag = tag, .tuple_type.args = args });
}

static struct ast* parse_prim_type(struct parser* parser, enum prim_type_tag tag) {
    struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, (enum token_tag)tag);
    return alloc_ast(parser, &begin_pos, &(struct ast) { .tag = AST_PRIM_TYPE, .prim_type.tag = tag });
}

static struct ast* parse_field_type(struct parser* parser) {
    struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    const char* name = NULL;
    if (parser->ahead[0].tag == TOK_IDENT && parser->ahead[1].tag == TOK_COLON) {
        name = parse_name(parser);
        expect_token(parser, TOK_COLON);
    }
    struct ast* arg = parse_type(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_FIELD_TYPE,
        .field_type = {
            .name = name,
            .arg = arg
        }
    });
}

static struct ast* parse_array_type(struct parser* parser) {
    const struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOK_LBRACKET);
    struct ast* elem_type = parse_type(parser);
    if (accept_token(parser, TOK_MUL)) {
        size_t elem_count = parser->ahead->tag == TOK_INT ? parser->ahead->int_val : 0;
        expect_token(parser, TOK_INT);
        return alloc_ast(parser, &begin_pos, &(struct ast) {
            .tag = AST_ARRAY_TYPE,
            .array_type = {
                .elem_type = elem_type,
                .elem_count = elem_count
            }
        });
    }
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_DYN_ARRAY_TYPE,
        .dyn_array_type.elem_type = elem_type
    });
}

static struct ast* parse_type(struct parser* parser) {
    switch (parser->ahead->tag) {
#define x(tag, ...) case TOK_##tag: return parse_prim_type(parser, PRIM_TYPE_##tag);
        PRIM_TYPE_LIST(x)
#undef x
        case TOK_LPAREN:
            return parse_tuple(parser, AST_TUPLE_TYPE, parse_type);
        case TOK_LBRACKET:
            if (parser->ahead[1].tag == TOK_RBRACKET ||
                (parser->ahead[1].tag == TOK_IDENT && parser->ahead[2].tag == TOK_COLON))
                return parse_record(parser, AST_RECORD_TYPE, parse_field_type);
            return parse_array_type(parser);
        default:
            return parse_error(parser, "type");
    }
}

static struct ast* parse_ident_pattern(struct parser* parser) {
    struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    const char* name = parse_name(parser);
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
        case TOK_IDENT:  return parse_ident_pattern(parser);
        case TOK_LPAREN: return parse_tuple(parser, AST_TUPLE_PATTERN, parse_pattern);
        default:         return parse_error(parser, "pattern");
    }
}

static struct ast* parse_func_param(struct parser* parser) {
    if (parser->ahead->tag == TOK_LPAREN)
        return parse_pattern(parser);
    return parse_error(parser, "function parameter");
}

static struct ast* parse_func_decl(struct parser* parser) {
    struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOK_FUNC);
    const char* name = parse_name(parser);
    struct ast* param = parse_func_param(parser);
    struct ast* ret_type = NULL;
    if (accept_token(parser, TOK_THIN_ARROW))
        ret_type = parse_type(parser);
    struct ast* body = NULL;
    if (parser->ahead->tag == TOK_LBRACE) {
        body = parse_expr(parser);
    } else if (accept_token(parser, TOK_EQ)) {
        body = parse_expr(parser);
        expect_token(parser, TOK_SEMICOLON);
    } else
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

static struct ast* parse_var_or_const_decl(struct parser* parser, bool is_const) {
    struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, is_const ? TOK_CONST : TOK_VAR);
    struct ast* pattern = parse_pattern(parser);
    struct ast* init = NULL;
    if (accept_token(parser, TOK_EQ))
        init = parse_expr(parser);
    else if (is_const)
        expect_token(parser, TOK_EQ);
    expect_token(parser, TOK_SEMICOLON);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = is_const ? AST_CONST_DECL : AST_VAR_DECL,
        .const_decl = {
            .pattern = pattern,
            .init = init
        }
    });
}

static struct ast* parse_decl(struct parser* parser) {
    switch (parser->ahead->tag) {
        case TOK_FUNC:  return parse_func_decl(parser);
        case TOK_CONST: return parse_var_or_const_decl(parser, true);
        case TOK_VAR:   return parse_var_or_const_decl(parser, false);
        default:        return parse_error(parser, "declaration");
    }
}

static struct ast* parse_field_expr(struct parser* parser) {
    struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    const char* name = NULL;
    if (parser->ahead[0].tag == TOK_IDENT && parser->ahead[1].tag == TOK_EQ) {
        name = parse_name(parser);
        expect_token(parser, TOK_EQ);
    }
    struct ast* arg = parse_expr(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_FIELD_EXPR,
        .field_expr = {
            .name = name,
            .arg = arg
        }
    });
}

static struct ast* parse_ident_expr(struct parser* parser) {
    struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    const char* name = parse_name(parser);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_IDENT_EXPR,
        .ident_expr.name = name
    });
}

static struct ast* parse_bool_literal(struct parser* parser, bool bool_val) {
    const struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, bool_val ? TOK_TRUE : TOK_FALSE);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_LITERAL,
        .literal = {
            .tag = LITERAL_BOOL,
            .bool_val = bool_val
        }
    });
}

static struct ast* parse_int_literal(struct parser* parser) {
    const struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    uintmax_t int_val = parser->ahead->int_val;
    eat_token(parser, TOK_INT);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_LITERAL,
        .literal = {
            .tag = LITERAL_INT,
            .int_val = int_val
        }
    });
}

static struct ast* parse_float_literal(struct parser* parser) {
    const struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    double float_val = parser->ahead->float_val;
    eat_token(parser, TOK_FLOAT);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_LITERAL,
        .literal = {
            .tag = LITERAL_FLOAT,
            .float_val = float_val
        }
    });
}

static struct ast* parse_array_expr(struct parser* parser) {
    const struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOK_LBRACKET);
    struct ast* elems = parse_many(parser, TOK_RBRACKET, TOK_COMMA, parse_expr);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_ARRAY_EXPR,
        .array_expr.elems = elems
    });
}

static struct ast* parse_block_expr(struct parser* parser) {
    const struct fir_source_pos begin_pos = parser->ahead->source_range.begin;
    eat_token(parser, TOK_LBRACE);
    bool ends_with_semicolon = false;
    struct ast* stmts = NULL;
    struct ast** prev = &stmts;
    while (parser->ahead->tag != TOK_RBRACE) {
        struct ast* stmt = NULL;
        ends_with_semicolon = false;
        switch (parser->ahead->tag) {
            case TOK_TRUE:
            case TOK_FALSE:
            case TOK_IDENT:
            case TOK_INT:
            case TOK_FLOAT:
            case TOK_LPAREN:
            case TOK_LBRACE:
            case TOK_LBRACKET:
                stmt = parse_expr(parser);
                break;
            case TOK_VAR:
            case TOK_CONST:
            case TOK_FUNC:
                stmt = parse_decl(parser);
                break;
            default:
                break;
        }
        if (!stmt)
            break;
        *prev = stmt;
        prev = &(*prev)->next;
        ends_with_semicolon = accept_token(parser, TOK_SEMICOLON);
        if (!ends_with_semicolon && ast_needs_semicolon(stmt))
            break;
    }
    expect_token(parser, TOK_RBRACE);
    return alloc_ast(parser, &begin_pos, &(struct ast) {
        .tag = AST_BLOCK_EXPR,
        .block_expr = {
            .stmts = stmts,
            .ends_with_semicolon = ends_with_semicolon
        }
    });
}

static struct ast* parse_expr(struct parser* parser) {
    switch (parser->ahead->tag) {
        case TOK_TRUE:     return parse_bool_literal(parser, true);
        case TOK_FALSE:    return parse_bool_literal(parser, false);
        case TOK_IDENT:    return parse_ident_expr(parser);
        case TOK_INT:      return parse_int_literal(parser);
        case TOK_FLOAT:    return parse_float_literal(parser);
        case TOK_LPAREN:   return parse_tuple(parser, AST_TUPLE_EXPR, parse_expr);
        case TOK_LBRACE:   return parse_block_expr(parser);
        case TOK_LBRACKET:
            if (parser->ahead[1].tag == TOK_RBRACKET ||
                (parser->ahead[1].tag == TOK_IDENT && parser->ahead[2].tag == TOK_EQ))
                return parse_record(parser, AST_RECORD_EXPR, parse_field_expr);
            return parse_array_expr(parser);
        default:
            return parse_error(parser, "expression");
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
