#include "fir/node.h"
#include "fir/module.h"

#include "support/format.h"

#include "analysis/scope.h"
#include "analysis/cfg.h"

#include <inttypes.h>

static inline void print_indent(struct format_out* out, size_t indent) {
    for (size_t i = 0; i < indent; ++i)
        format(out, "    ");
}

static void print_fp_flags(struct format_out* out, enum fir_fp_flags flags) {
    format(out, "[");
    if (flags & FIR_FP_FINITE_ONLY)    format(out, "+fo");
    if (flags & FIR_FP_NO_SIGNED_ZERO) format(out, "+nsz");
    if (flags & FIR_FP_ASSOCIATIVE)    format(out, "+a");
    if (flags & FIR_FP_DISTRIBUTIVE)   format(out, "+d");
    format(out, "]");
}

static void print_node(struct format_out* out, const struct fir_node* node) {
    format(out, "%s", fir_node_tag_to_string(node->tag));
    if (fir_node_is_nominal(node) && node->data.linkage != FIR_INTERNAL)
        format(out, "[%s]", node->data.linkage == FIR_IMPORTED ? "imported" : "exported");
    else if (fir_node_has_bitwidth(node))
        format(out, "[%zu]", node->data.bitwidth);
    else if (node->tag == FIR_CONST && node->ty->tag == FIR_INT_TY)
        format(out, "[%"PRIu64"]", node->data.int_val);
    else if (node->tag == FIR_CONST && node->ty->tag == FIR_FLOAT_TY)
        format(out, "[%a]", node->data.float_val);
    else if (node->tag == FIR_ARRAY_TY)
        format(out, "[%zu]", node->data.array_dim);
    else if (fir_node_has_fp_flags(node))
        print_fp_flags(out, node->data.fp_flags);
    if (node->op_count == 0)
        return;
    format(out, "(");
    for (size_t i = 0; i < node->op_count; ++i) {
        if (!node->ops[i])
            format(out, "<unset>");
        else if (fir_node_is_ty(node->ops[i]))
            print_node(out, node->ops[i]);
        else
            format(out, "%s_%"PRIu64, fir_node_name(node->ops[i]), node->ops[i]->id);
        if (i != node->op_count - 1)
            format(out, ", ");
    }
    format(out, ")");
}

static void print_node_with_ty(struct format_out* out, const struct fir_node* node, size_t indent) {
    print_indent(out, indent);
    if (fir_node_is_ty(node)) {
        print_node(out, node);
    } else {
        print_node(out, node->ty);
        format(out, " %s_%"PRIu64" = ", fir_node_name(node), node->id);
        print_node(out, node);
    }
}

void fir_node_print_to_file(FILE* file, const struct fir_node* node, size_t indent) {
    print_node_with_ty(&(struct format_out) { .tag = FORMAT_OUT_FILE, .file = file }, node, indent);
}

size_t fir_node_print_to_buf(char* buf, size_t size, const struct fir_node* node, size_t indent) {
    struct format_buf format_buf = { .data = buf, .capacity = size };
    print_node_with_ty(&(struct format_out) { .tag = FORMAT_OUT_BUF, .format_buf = &format_buf }, node, indent);
    return format_buf.size;
}

void fir_node_dump(const struct fir_node* node) {
    fir_node_print_to_file(stdout, node, 0);
    fflush(stdout);
}

static inline void print_mod(struct format_out* out, const struct fir_mod* mod) {
    struct fir_node* const* globals = fir_mod_globals(mod);
    struct fir_node* const* funcs = fir_mod_funcs(mod);
    size_t func_count = fir_mod_func_count(mod);
    size_t global_count = fir_mod_global_count(mod);

    for (size_t i = 0; i < global_count; ++i) {
        print_node_with_ty(out, globals[i], 0);
        format(out, "\n");
    }

    for (size_t i = 0; i < func_count; ++i) {
        if (funcs[i]->ty->ops[1]->tag == FIR_NORET_TY)
            continue;

        print_node_with_ty(out, funcs[i], 0);
        format(out, "\n");
        struct scope scope = scope_create(funcs[i]);
        struct cfg cfg = cfg_create(&scope);
        SET_FOREACH(const struct fir_node*, node_ptr, scope.nodes) {
            print_node_with_ty(out, *node_ptr, 1);
            format(out, "\n");
        }
        node_graph_dump(&cfg.graph);
        scope_destroy(&scope);
        cfg_destroy(&cfg);
        format(out, "\n");
    }
}

void fir_mod_print_to_file(FILE* file, const struct fir_mod* mod) {
    print_mod(&(struct format_out) { .tag = FORMAT_OUT_FILE, .file = file }, mod);
}

size_t fir_mod_print_to_buf(char* buf, size_t size, const struct fir_mod* mod) {
    struct format_buf format_buf = { .data = buf, .capacity = size };
    print_mod(&(struct format_out) { .tag = FORMAT_OUT_BUF, .format_buf = &format_buf }, mod);
    return format_buf.size;
}

void fir_mod_dump(const struct fir_mod* mod) {
    fir_mod_print_to_file(stdout, mod);
    fflush(stdout);
}
