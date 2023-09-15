#include "fir/node.h"

#include "support/print.h"

#include <inttypes.h>
#include <assert.h>

#define x(tag, ...) case FIR_##tag:
#define pred_func(f, list) \
    bool f (enum node_tag tag) { \
        switch (tag) { \
            list(x) \
                return true; \
            default: \
                return false; \
        } \
    }

pred_func(fir_node_tag_is_ty, FIR_TYPE_LIST)
pred_func(fir_node_tag_is_nominal, FIR_NOMINAL_NODE_LIST)
pred_func(fir_node_tag_is_iarithop, FIR_IARITH_OP_LIST)
pred_func(fir_node_tag_is_farithop, FIR_FARITH_OP_LIST)
pred_func(fir_node_tag_is_icmpop, FIR_ICMP_OP_LIST)
pred_func(fir_node_tag_is_fcmpop, FIR_FCMP_OP_LIST)
pred_func(fir_node_tag_is_idivop, FIR_IDIV_OP_LIST)
pred_func(fir_node_tag_is_fdivop, FIR_FDIV_OP_LIST)
pred_func(fir_node_tag_is_bitop, FIR_BIT_OP_LIST)
pred_func(fir_node_tag_is_castop, FIR_CAST_OP_LIST)
pred_func(fir_node_tag_is_aggrop, FIR_AGGR_OP_LIST)
pred_func(fir_node_tag_is_memop, FIR_MEM_OP_LIST)
pred_func(fir_node_tag_is_controlop, FIR_CONTROL_OP_LIST)

#undef pred_func
#undef x

const char* fir_node_tag_to_string(enum node_tag tag) {
    switch (tag) {
#define x(tag, str) case FIR_##tag: return str;
        FIR_NODE_LIST(x)
#undef x
        default:
            assert(false && "invalid node tag");
            return "";
    }
}

struct fir_mod* fir_node_mod(const struct fir_node* node) {
    return fir_node_is_ty(node) ? node->mod : node->ty->mod;
}

const char* fir_node_name(const struct fir_node* node) {
    return node->dbg_info ? node->dbg_info->name : "";
}

static void print_fp_flags(enum fir_fp_flags flags) {
    if (flags == FIR_FP_STRICT) printf("strict ");
    else if (flags == FIR_FP_FAST) printf("fast ");
    else {
        if (flags & FIR_FP_FINITE_ONLY)    printf("finite-only ");
        if (flags & FIR_FP_NO_SIGNED_ZERO) printf("no-signed-zero ");
        if (flags & FIR_FP_ASSOCIATIVE)    printf("associative ");
        if (flags & FIR_FP_DISTRIBUTIVE)   printf("distributive ");
    }
}

static void print_node(const struct fir_node* node, bool recurse) {
    if (node->tag == FIR_INT_TY) {
        printf("int%"PRIu32, node->bitwidth);
    } else if (node->tag == FIR_FLOAT_TY) {
        printf("float%"PRIu32, node->bitwidth);
    } else if (node->tag == FIR_CONST) {
        if (node->ty->tag == FIR_INT_TY)
            printf("%"PRIu64, node->int_val);
        else
            printf("%g", node->float_val);
    } else if (node->tag == FIR_ARRAY_TY) {
        printf("array[");
        print_node(node->ops[0], true);
        printf(" * %"PRIu64"]", node->array_dim);
    } else {
        if (fir_node_has_fp_flags(node))
            print_fp_flags(node->fp_flags);
        printf("%s%c", fir_node_tag_to_string(node->tag), fir_node_is_ty(node) ? '[' : '(');
        for (size_t i = 0; i < node->op_count; ++i) {
            if (!node->ops[i])
                printf("<unset>");
            else if (recurse)
                print_node(node->ops[i], true);
            else
                printf("%s_%"PRIu64, fir_node_name(node->ops[i]), node->ops[i]->id);
            if (i != node->op_count - 1)
                printf(", ");
        }
        printf("%c", fir_node_is_ty(node) ? ']' : ')');
    }
}

void fir_node_print_with_indent(const struct fir_node* node, size_t indent) {
    print_indent(indent);
    if (fir_node_is_ty(node)) {
        print_node(node, true);
    } else {
        print_node(node->ty, true);
        printf(" %s_%"PRIu64" = ", fir_node_name(node), node->id);
        print_node(node, false);
    }
}
