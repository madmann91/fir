#include "fir/module.h"

#include "support/parser.h"
#include "support/vec.h"
#include "support/map.h"
#include "support/str.h"
#include "support/hash.h"
#include "support/bits.h"

#include <stdio.h>

#define SMALL_OP_COUNT

static inline uint32_t hash_str_view(const struct str_view* str_view) {
    uint32_t h = hash_init();
    for (size_t i = 0; i < str_view->length; ++i)
        h = hash_uint8(h, str_view->data[i]);
    return h;
}

static inline bool cmp_str_view(const struct str_view* str_view, const struct str_view* other) {
    return str_view_is_equal(*str_view, *other);
}

struct delayed_op {
    size_t op_index;
    struct str_view op_name;
    struct fir_source_range source_range;
    struct fir_node* nominal_node;
};

DECL_SMALL_VEC(op_vec, const struct fir_node*)
DECL_VEC(delayed_op_vec, struct delayed_op)
DECL_MAP(symbol_table, struct str_view, const struct fir_node*, hash_str_view, cmp_str_view)

struct parse_context {
    struct fir_mod* mod;
    struct fir_dbg_info_pool* dbg_pool;
    struct symbol_table symbol_table;
    const char* file_name;
    struct delayed_op_vec delayed_ops;
    struct parser parser;
    size_t error_count;
    FILE* error_log;
};

static void report_error(
    void* error_data,
    const struct fir_source_range* source_range,
    const char* fmt,
    va_list args)
{
    struct parse_context* context = (struct parse_context*)error_data;
    if (context->error_log) {
        fprintf(context->error_log, "error in %s(%d:%d - %d:%d): ",
            context->file_name,
            source_range->begin.row,
            source_range->begin.col,
            source_range->end.row,
            source_range->end.col);
        vfprintf(context->error_log, fmt, args);
        fprintf(context->error_log, "\n");
    }
    context->error_count++;
}

static inline void unknown_identifier(
    struct parse_context* context,
    const struct fir_source_range* ident_range,
    struct str_view ident)
{
    parser_error(&context->parser,
        ident_range,
        "unknown identifier '%.*s'",
        (int)ident.length, ident.data);
}

static inline const struct fir_node* parse_ty(struct parse_context*);

static inline uint64_t parse_int_val(struct parse_context* context) {
    uint64_t int_val = context->parser.ahead->int_val;
    parser_expect(&context->parser, TOK_INT);
    return int_val;
}

static inline double parse_float_val(struct parse_context* context) {
    double float_val = context->parser.ahead->float_val;
    parser_expect(&context->parser, TOK_FLOAT);
    if (parser_accept(&context->parser, TOK_LPAREN)) {
        parser_expect(&context->parser, TOK_FLOAT);
        parser_expect(&context->parser, TOK_RPAREN);
    }
    return float_val;
}

static inline void invalid_fp_flag(
    struct parse_context* context,
    const struct fir_source_range* source_range,
    struct str_view fp_flag)
{
    parser_error(&context->parser, source_range,
        "invalid floating-point flag '%.*s'",
        (int)fp_flag.length, fp_flag.data);
}

static inline enum fir_fp_flags parse_fp_flags(struct parse_context* context) {
    enum fir_fp_flags fp_flags = FIR_FP_STRICT;
    while (parser_accept(&context->parser, TOK_PLUS)) {
        struct str_view ident = context->parser.ahead->str;
        if (str_view_is_equal(ident, str_view("fo")))       fp_flags |= FIR_FP_FINITE_ONLY;
        else if (str_view_is_equal(ident, str_view("nsz"))) fp_flags |= FIR_FP_NO_SIGNED_ZERO;
        else if (str_view_is_equal(ident, str_view("a")))   fp_flags |= FIR_FP_ASSOCIATIVE;
        else if (str_view_is_equal(ident, str_view("d")))   fp_flags |= FIR_FP_DISTRIBUTIVE;
        else invalid_fp_flag(context, &context->parser.ahead->source_range, ident);

        parser_expect(&context->parser, TOK_IDENT);
    }
    return fp_flags;
}

static inline uint64_t parse_array_dim(struct parse_context* context) {
    return parse_int_val(context);
}

static inline uint32_t parse_bitwidth(struct parse_context* context) {
    return parse_int_val(context);
}

static inline const struct fir_node* parse_no_arg_ty(struct parse_context* context) {
    parser_next(&context->parser);
    switch (context->parser.ahead->tag) {
        case TOK_NORET_TY: return fir_noret_ty(context->mod);
        case TOK_ERR_TY:   return fir_err_ty(context->mod);
        case TOK_MEM_TY:   return fir_mem_ty(context->mod);
        case TOK_PTR_TY:   return fir_ptr_ty(context->mod);
        default:
            assert(false && "invalid type tag");
            return NULL;
    }
}

static inline const struct fir_node* parse_int_or_float_ty(struct parse_context* context) {
    bool is_int = context->parser.ahead->tag == TOK_INT_TY;
    parser_next(&context->parser);
    parser_expect(&context->parser, TOK_LBRACKET);
    uint32_t bitwidth = parse_bitwidth(context);
    parser_expect(&context->parser, TOK_RBRACKET);
    return is_int ? fir_int_ty(context->mod, bitwidth) : fir_float_ty(context->mod, bitwidth);
}

static inline const struct fir_node* parse_array_ty(struct parse_context* context) {
    parser_eat(&context->parser, TOK_ARRAY_TY);
    parser_expect(&context->parser, TOK_LBRACKET);
    uint64_t array_dim = parse_array_dim(context);
    parser_expect(&context->parser, TOK_RBRACKET);
    parser_expect(&context->parser, TOK_LPAREN);
    const struct fir_node* elem_ty = parse_ty(context);
    parser_expect(&context->parser, TOK_RPAREN);
    return fir_array_ty(elem_ty, array_dim);
}

static inline const struct fir_node* parse_dynarray_ty(struct parse_context* context) {
    parser_eat(&context->parser, TOK_DYNARRAY_TY);
    parser_expect(&context->parser, TOK_LPAREN);
    const struct fir_node* elem_ty = parse_ty(context);
    parser_expect(&context->parser, TOK_RPAREN);
    return fir_dynarray_ty(elem_ty);
}

static inline const struct fir_node* parse_func_ty(struct parse_context* context) {
    parser_eat(&context->parser, TOK_FUNC_TY);
    parser_expect(&context->parser, TOK_LPAREN);
    const struct fir_node* param_ty = parse_ty(context);
    parser_expect(&context->parser, TOK_COMMA);
    const struct fir_node* ret_ty = parse_ty(context);
    parser_expect(&context->parser, TOK_RPAREN);
    return fir_func_ty(param_ty, ret_ty);
}

static inline const struct fir_node* parse_tup_ty(struct parse_context* context) {
    parser_eat(&context->parser, TOK_TUP_TY);
    struct op_vec ops;
    op_vec_init(&ops);
    parser_expect(&context->parser, TOK_LPAREN);
    while (context->parser.ahead->tag != TOK_RPAREN) {
        const struct fir_node* op = parse_ty(context);
        op_vec_push(&ops, &op);
        if (!parser_accept(&context->parser, TOK_COMMA))
            break;
    }
    parser_expect(&context->parser, TOK_RPAREN);
    const struct fir_node* tup_ty = fir_tup_ty(context->mod, ops.elems, ops.elem_count);
    op_vec_destroy(&ops);
    return tup_ty;
}

static inline const struct fir_node* parse_ty(struct parse_context* context) {
    switch (context->parser.ahead->tag) {
        case TOK_ARRAY_TY:    return parse_array_ty(context);
        case TOK_DYNARRAY_TY: return parse_dynarray_ty(context);
        case TOK_FUNC_TY:     return parse_func_ty(context);
        case TOK_TUP_TY:      return parse_tup_ty(context);
        case TOK_NORET_TY:
        case TOK_ERR_TY:
        case TOK_MEM_TY:
        case TOK_PTR_TY:
            return parse_no_arg_ty(context);
        case TOK_INT_TY:
        case TOK_FLOAT_TY:
            return parse_int_or_float_ty(context);
        default:
        {
            struct str_view token_str = context->parser.ahead->str;
            parser_error(&context->parser,
                &context->parser.ahead->source_range,
                "expected type, but got '%.*s'",
                (int)token_str.length, token_str.data);
            parser_next(&context->parser);
            return NULL;
        }
    }
}

static inline struct str_view parse_ident(struct parse_context* context) {
    struct str_view ident = context->parser.ahead->str;
    parser_expect(&context->parser, TOK_IDENT);
    return ident;
}

static inline const struct fir_node* parse_op(
    struct parse_context* context,
    struct fir_node* nominal_node,
    size_t op_index)
{
    if (token_tag_is_ty(context->parser.ahead->tag))
        return parse_ty(context);
    struct fir_source_range ident_range = context->parser.ahead->source_range; 
    struct str_view ident = parse_ident(context);
    const struct fir_node* const* symbol = symbol_table_find(&context->symbol_table, &ident);
    if (symbol)
        return *symbol;
    if (nominal_node) {
        delayed_op_vec_push(&context->delayed_ops, &(struct delayed_op) {
            .nominal_node = nominal_node,
            .op_index = op_index,
            .source_range = ident_range,
            .op_name = ident
        });
    } else {
        unknown_identifier(context, &ident_range, ident);
    }
    return NULL;
}

static inline union fir_node_data parse_node_data(
    struct parse_context* context,
    enum fir_node_tag tag,
    const struct fir_node* ty)
{
    union fir_node_data data = {};
    if (tag == FIR_CONST || fir_node_tag_has_fp_flags(tag)) {
        parser_expect(&context->parser, TOK_LBRACKET);
        if (tag == FIR_CONST && ty->tag == FIR_INT_TY) {
            data.int_val = parse_int_val(context) & make_bitmask(ty->data.bitwidth);
        } else if (tag == FIR_CONST && ty->tag == FIR_FLOAT_TY) {
            data.float_val = parse_float_val(context);
        } else {
            assert(fir_node_tag_has_fp_flags(tag));
            data.fp_flags = parse_fp_flags(context);
        }
        parser_expect(&context->parser, TOK_RBRACKET);
    }
    return data;
}

static inline const struct fir_node* parse_node_body(
    struct parse_context* context,
    const struct fir_node* ty)
{
    if (!token_tag_is_node_tag(context->parser.ahead->tag)) {
        parser_next(&context->parser);
        parser_error(
            &context->parser,
            &context->parser.ahead->source_range,
            "expected node tag, but got '%s'",
            token_tag_to_string(context->parser.ahead->tag));
        return NULL;
    }

    enum fir_node_tag tag = (enum fir_node_tag)context->parser.ahead->tag;
    parser_next(&context->parser);

    union fir_node_data data = parse_node_data(context, tag, ty);

    struct fir_node* nominal_node = NULL;
    if (tag == FIR_FUNC)
        nominal_node = fir_func(ty);

    bool valid_ops = true;
    size_t op_count = 0;
    struct op_vec ops;
    op_vec_init(&ops);
    if (parser_accept(&context->parser, TOK_LPAREN)) {
        while (context->parser.ahead->tag != TOK_RPAREN) {
            const struct fir_node* op = parse_op(context, nominal_node, op_count);
            if (!nominal_node) {
                op_vec_push(&ops, &op);
                valid_ops &= op != NULL;
            } else if (op) {
                fir_node_set_op(nominal_node, op_count, op);
            }
            op_count++;
            if (!parser_accept(&context->parser, TOK_COMMA))
                break;
        }
        parser_expect(&context->parser, TOK_RPAREN);
    }

    if (nominal_node || !valid_ops) {
        op_vec_destroy(&ops);
        return nominal_node;
    }

    const struct fir_node* node = fir_node_rebuild(
        context->mod,
        &(struct fir_node) {
            .tag = tag,
            .data = data,
            .op_count = ops.elem_count
        }, ty, ops.elems);
    assert(node->ty == ty);

    op_vec_destroy(&ops);
    return node;
}

static inline const struct fir_node* parse_node(struct parse_context* context) {
    const struct fir_node* ty = parse_ty(context);
    if (!ty)
        return NULL;
    struct fir_source_range ident_range = context->parser.ahead->source_range;
    struct str_view ident = parse_ident(context);
    parser_expect(&context->parser, TOK_EQ);
    const struct fir_node* body = parse_node_body(context, ty);
    if (body) {
        if (!symbol_table_insert(&context->symbol_table, &ident, &body)) {
            parser_error(&context->parser,
                &ident_range,
                "identifier '%.*s' already exists",
                (int)ident.length, ident.data);
        }
    }
    return body;
}

bool fir_mod_parse(struct fir_mod* mod, const struct fir_parse_input* input) {
    struct parse_context context = {
        .mod = mod,
        .error_log = input->error_log,
        .dbg_pool = input->dbg_pool,
        .file_name = input->file_name,
        .delayed_ops = delayed_op_vec_create(),
        .parser = parser_create(input->file_data, input->file_size),
        .symbol_table = symbol_table_create()
    };

    context.parser.error_data = &context;
    context.parser.error_func = report_error;

    while (context.parser.ahead->tag != TOK_EOF)
        parse_node(&context);

    FOREACH_VEC(const struct delayed_op, delayed_op, context.delayed_ops) {
        const struct fir_node* const* op = symbol_table_find(&context.symbol_table, &delayed_op->op_name);
        if (op)
            fir_node_set_op(delayed_op->nominal_node, delayed_op->op_index, *op);
        else
            unknown_identifier(&context, &delayed_op->source_range, delayed_op->op_name);
    }

    delayed_op_vec_destroy(&context.delayed_ops);
    symbol_table_destroy(&context.symbol_table);
    return context.error_count == 0;
}
