#ifndef FIR_NODE_H
#define FIR_NODE_H

#include "fir/platform.h"
#include "fir/fp_flags.h"
#include "fir/dbg_info.h"
#include "fir/node_list.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

struct fir_mod;
struct fir_node;

/// @file
///
/// IR nodes can either represent types or values. They are always created via a module, which allows
/// both hash-consing and node simplification to take place on the fly. Nodes have a unique ID, which
/// is given to them by the module, and also reflects the order in which they were created.

/// A tag that identifies the sort of type or value that a node represents.
enum fir_node_tag {
/// @cond PRIVATE
#define x(tag, ...) FIR_##tag,
/// @endcond
    FIR_NODE_LIST(x)
#undef x
};

/// A _use_ of a node by another node.
struct fir_use {
    size_t index;                ///< The operand index where the node is used.
    const struct fir_node* user; ///< The node which is using the node being considered.
    const struct fir_use* next;  ///< Next use in the list, or NULL.
};

/// Node data that is not representable via operands.
union fir_node_data {
    enum fir_fp_flags fp_flags; ///< Floating-point flags, for floating-point instructions.
    uint64_t int_val;           ///< Integer value, for integer constants.
    double float_val;           ///< Floating-point value, for floating-point constants.
    uint32_t bitwidth;          ///< Bitwidth, for integer or floating-point types.
    uint64_t array_dim;         ///< Array dimension, for fixed-size array types.
};

/// Members of the node structure. @see fir_node
#define FIR_NODE(n) \
    uint64_t id; \
    enum fir_node_tag tag; \
    union fir_node_data data; \
    const struct fir_use* uses; \
    const struct fir_dbg_info* dbg_info; \
    size_t op_count; \
    union { \
        const struct fir_node* ty; \
        struct fir_mod* mod; \
    }; \
    const struct fir_node* ops[n];

/// @struct fir_node
/// IR node.
struct fir_node { FIR_NODE() };

/// @name Predicates
/// @{

FIR_SYMBOL bool fir_node_tag_is_ty(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_nominal(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_iarith_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_farith_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_icmp_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_fcmp_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_idiv_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_fdiv_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_bit_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_cast_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_aggr_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_mem_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_control_op(enum fir_node_tag);

static inline bool fir_node_tag_has_fp_flags(enum fir_node_tag tag) { return fir_node_tag_is_fdiv_op(tag) || fir_node_tag_is_farith_op(tag); }
static inline bool fir_node_tag_has_bitwidth(enum fir_node_tag tag) { return tag == FIR_INT_TY || tag == FIR_FLOAT_TY; }

static inline bool fir_node_is_ty(const struct fir_node* n)        { return fir_node_tag_is_ty(n->tag); }
static inline bool fir_node_is_nominal(const struct fir_node* n)   { return fir_node_tag_is_nominal(n->tag); }
static inline bool fir_node_is_iarith_op(const struct fir_node* n)  { return fir_node_tag_is_iarith_op(n->tag); }
static inline bool fir_node_is_farith_op(const struct fir_node* n)  { return fir_node_tag_is_farith_op(n->tag); }
static inline bool fir_node_is_icmp_op(const struct fir_node* n)    { return fir_node_tag_is_icmp_op(n->tag); }
static inline bool fir_node_is_fcmp_op(const struct fir_node* n)    { return fir_node_tag_is_fcmp_op(n->tag); }
static inline bool fir_node_is_idiv_op(const struct fir_node* n)    { return fir_node_tag_is_idiv_op(n->tag); }
static inline bool fir_node_is_fdiv_op(const struct fir_node* n)    { return fir_node_tag_is_fdiv_op(n->tag); }
static inline bool fir_node_is_bit_op(const struct fir_node* n)     { return fir_node_tag_is_bit_op(n->tag); }
static inline bool fir_node_is_cast_op(const struct fir_node* n)    { return fir_node_tag_is_cast_op(n->tag); }
static inline bool fir_node_is_aggr_op(const struct fir_node* n)    { return fir_node_tag_is_aggr_op(n->tag); }
static inline bool fir_node_is_mem_op(const struct fir_node* n)     { return fir_node_tag_is_mem_op(n->tag); }
static inline bool fir_node_is_control_op(const struct fir_node* n) { return fir_node_tag_is_control_op(n->tag); }
static inline bool fir_node_has_fp_flags(const struct fir_node* n) { return fir_node_tag_has_fp_flags(n->tag); }
static inline bool fir_node_has_bitwidth(const struct fir_node* n) { return fir_node_tag_has_bitwidth(n->tag); }

/// @}

/// Converts the given node tag to a human-readable string.
FIR_SYMBOL const char* fir_node_tag_to_string(enum fir_node_tag);
/// Returns the module that the given node was created from.
FIR_SYMBOL struct fir_mod* fir_node_mod(const struct fir_node*);
/// Returns the name of the given node, based on its debug information (if any).
FIR_SYMBOL const char* fir_node_name(const struct fir_node*);

/// Rebuilds the given structural node with new operands and type into the given module.
/// Constant values and other node-specific data is taken from the original node.
FIR_SYMBOL const struct fir_node* fir_node_rebuild(
    struct fir_mod* mod,
    struct fir_node* node,
    const struct fir_node* ty,
    const struct fir_node* const* ops);

/// Sets the operand of a nominal node.
FIR_SYMBOL void fir_node_set_op(struct fir_node* node, size_t op_index, const struct fir_node* op);
/// Prints a node on the given stream with the given indentation level.
FIR_SYMBOL void fir_node_print(FILE*, const struct fir_node*, size_t indent);
/// Prints a node on standard output.
FIR_SYMBOL void fir_node_dump(const struct fir_node*);

#endif
