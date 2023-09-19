#include "fir/node.h"
#include "fir/module.h"

#include <inttypes.h>

static inline void print_indent(FILE* file, size_t indent) {
    for (size_t i = 0; i < indent; ++i)
        fprintf(file, "    ");
}

static void print_fp_flags(FILE* file, enum fir_fp_flags flags) {
    fprintf(file, "[");
    if (flags & FIR_FP_FINITE_ONLY)    fprintf(file, "+fo");
    if (flags & FIR_FP_NO_SIGNED_ZERO) fprintf(file, "+nsz");
    if (flags & FIR_FP_ASSOCIATIVE)    fprintf(file, "+a");
    if (flags & FIR_FP_DISTRIBUTIVE)   fprintf(file, "+d");
    fprintf(file, "]");
}

static void print_node(FILE* file, const struct fir_node* node) {
    fprintf(file, "%s", fir_node_tag_to_string(node->tag));
    if (fir_node_has_bitwidth(node))
        fprintf(file, "[%"PRIu32"]", node->data.bitwidth);
    else if (node->tag == FIR_CONST && node->ty->tag == FIR_INT_TY)
        fprintf(file, "[%"PRIu64"]", node->data.int_val);
    else if (node->tag == FIR_CONST && node->ty->tag == FIR_FLOAT_TY)
        fprintf(file, "[%a]", node->data.float_val);
    else if (node->tag == FIR_ARRAY_TY)
        fprintf(file, "[%"PRIu64"]", node->data.array_dim);
    else if (fir_node_has_fp_flags(node))
        print_fp_flags(file, node->data.fp_flags);
    if (node->op_count == 0)
        return;
    fprintf(file, "(");
    for (size_t i = 0; i < node->op_count; ++i) {
        if (!node->ops[i])
            fprintf(file, "<unset>");
        else if (fir_node_is_ty(node->ops[i]))
            print_node(file, node->ops[i]);
        else
            fprintf(file, "%s_%"PRIu64, fir_node_name(node->ops[i]), node->ops[i]->id);
        if (i != node->op_count - 1)
            fprintf(file, ", ");
    }
    fprintf(file, ")");
}

void fir_node_print(FILE* file, const struct fir_node* node, size_t indent) {
    print_indent(file, indent);
    if (fir_node_is_ty(node)) {
        print_node(file, node);
    } else {
        print_node(file, node->ty);
        fprintf(file, " %s_%"PRIu64" = ", fir_node_name(node), node->id);
        print_node(file, node);
    }
}

void fir_node_dump(const struct fir_node* node) {
    fir_node_print(stdout, node, 0);
}

void fir_mod_print(FILE* file, const struct fir_mod* mod) {
    struct fir_node** funcs = fir_mod_funcs(mod);
    for (size_t i = 0; i < fir_mod_func_count(mod); ++i) {
        fir_node_print(file, funcs[i], 0);
        fprintf(file, "\n");
    }
}

void fir_mod_dump(const struct fir_mod* mod) {
    fir_mod_print(stdout, mod);
}
