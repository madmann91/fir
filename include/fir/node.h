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

/// Linkage for nominal nodes.
enum fir_linkage {
    FIR_INTERNAL, ///< The nominal node is internal to the module and is defined in it.
    FIR_EXPORTED, ///< The nominal node is defined in the module and exported.
    FIR_IMPORTED  ///< The nominal node is defined outside of the module and imported.
};

/// A _use_ of a node by another node.
struct fir_use {
    size_t index;                ///< The operand index where the node is used.
    const struct fir_node* user; ///< The node which is using the node being considered.
    const struct fir_use* next;  ///< Next use in the list, or NULL.
};

typedef uint64_t fir_int_val;
typedef double fir_float_val;

/// Node data that is not representable via operands.
union fir_node_data {
    enum fir_linkage linkage;   ///< Linkage mode, for nominal nodes.
    enum fir_fp_flags fp_flags; ///< Floating-point flags, for floating-point instructions.
    fir_int_val int_val;        ///< Integer value, for integer constants.
    fir_float_val float_val;    ///< Floating-point value, for floating-point constants.
    size_t bitwidth;            ///< Bitwidth, for integer or floating-point types.
    size_t array_dim;           ///< Array dimension, for fixed-size array types.
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
/// IR node. @see FIR_NODE
struct fir_node { FIR_NODE() };

/// @name Predicates
/// @{

FIR_SYMBOL bool fir_node_tag_is_ty(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_nominal(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_iarith_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_farith_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_icmp_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_fcmp_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_bit_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_cast_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_aggr_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_mem_op(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_is_control_op(enum fir_node_tag);

FIR_SYMBOL bool fir_node_tag_has_fp_flags(enum fir_node_tag);
FIR_SYMBOL bool fir_node_tag_has_bitwidth(enum fir_node_tag);

FIR_SYMBOL bool fir_node_is_ty(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_nominal(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_iarith_op(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_farith_op(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_icmp_op(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_fcmp_op(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_bit_op(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_cast_op(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_aggr_op(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_mem_op(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_control_op(const struct fir_node*);

FIR_SYMBOL bool fir_node_has_fp_flags(const struct fir_node*);
FIR_SYMBOL bool fir_node_has_bitwidth(const struct fir_node*);

FIR_SYMBOL bool fir_node_is_int_const(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_float_const(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_bool_ty(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_not(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_ineg(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_fneg(const struct fir_node*);

FIR_SYMBOL bool fir_node_is_zero(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_one(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_all_ones(const struct fir_node*);

FIR_SYMBOL bool fir_node_is_select(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_choice(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_jump(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_branch(const struct fir_node*);
FIR_SYMBOL bool fir_node_is_switch(const struct fir_node*);

/// @}

/// Converts the given node tag to a human-readable string.
FIR_SYMBOL const char* fir_node_tag_to_string(enum fir_node_tag);
/// Returns the module that the given node was created from.
FIR_SYMBOL struct fir_mod* fir_node_mod(const struct fir_node*);
/// Returns the name of the given node, based on its debug information (if any).
FIR_SYMBOL const char* fir_node_name(const struct fir_node*);
/// Sets the debug information attached to the node.
FIR_SYMBOL void fir_node_set_dbg_info(const struct fir_node*, const struct fir_dbg_info*);
/// Sets the operand of a nominal node.
FIR_SYMBOL void fir_node_set_op(struct fir_node* node, size_t op_index, const struct fir_node* op);

/// Rebuilds the given _structural_ node with new operands and type into the given module.
/// Constant values and other node-specific data is taken from the original node.
FIR_SYMBOL const struct fir_node* fir_node_rebuild(
    struct fir_mod*,
    const struct fir_node* node,
    const struct fir_node* ty,
    const struct fir_node* const* ops);

/// Clones the given _nominal_ node with a new type into the given module.
/// Linkage is inherited from the original nominal node.
FIR_SYMBOL struct fir_node* fir_node_clone(
    struct fir_mod*,
    const struct fir_node* nominal_node,
    const struct fir_node* ty);

/// Prints a node on the given stream with the given indentation level.
FIR_SYMBOL void fir_node_print(FILE*, const struct fir_node*, size_t indent);
/// Prints a node on standard output.
FIR_SYMBOL void fir_node_dump(const struct fir_node*);

/// Counts the number of uses in the list.
FIR_SYMBOL size_t fir_use_count(const struct fir_use*);

#endif
