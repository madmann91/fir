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
    const struct fir_use* next;  ///< Next use in the list, or `NULL`.
};

/// Integer constant value. Only the first `n` bits are used for an integer constant with a
/// bitwidth of `n`, and the rest are set to `0`.
typedef uint64_t fir_int_val;

/// Floating-point constant value. This is the data type used for storage, but operations on the
/// constant will use the floating-point number format corresponding to the constant type. For
/// instance, when adding two 32-bit floating-point constants together, they are internally
/// converted to 32-bit floating-point numbers before the performing the addition, and the result is
/// widened back to this type before storing it into another constant. This is only used to
/// represent constants in the IR, and, of course, when generating code, the type used for storage
/// is the exact floating-point type.
typedef double fir_float_val;

/// Flags for memory operations.
enum fir_mem_flags {
    FIR_MEM_NON_NULL = 0x01, ///< Pointer arguments are non-null.
    FIR_MEM_VOLATILE = 0x02  ///< The value pointed to may change outside of the program.
};

/// Flags for functions.
enum fir_func_flags {
    /// A function is deemed pure when it has no side-effects, always terminates, and produces the
    /// same return value given the same input values.
    FIR_FUNC_PURE = 0x01,
};

/// Node data that is not representable via operands.
union fir_node_data {
    enum fir_func_flags func_flags; ///< Flags for functions.
    enum fir_mem_flags mem_flags;   ///< Flags for operations that deal with memory.
    enum fir_fp_flags fp_flags;     ///< Floating-point flags, for floating-point instructions.
    fir_int_val int_val;            ///< Integer value, for integer constants.
    fir_float_val float_val;        ///< Floating-point value, for floating-point constants.
    size_t bitwidth;                ///< Bitwidth, for integer or floating-point types.
    size_t array_dim;               ///< Array dimension, for fixed-size array types.
};

/// Properties of structural nodes.
enum fir_node_props {
    /// The value of invariant nodes do not depend on a parameter, directly or indirectly. Such
    /// nodes are constants from the point of view of the IR.
    FIR_PROP_INVARIANT = 0x01,
    /// Nodes that do not generate side-effects during evaluation are considered speculatable, which
    /// is an indication for the scheduler that they can be moved in locations that produce
    /// partially-dead code.
    FIR_PROP_SPECULATABLE = 0x02
};

/// Members of the @ref fir_node structure.
#define FIR_NODE(n) \
    uint64_t id; \
    enum fir_node_tag tag; \
    enum fir_node_props props; \
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
/// IR node. @see FIR_NODE.
struct fir_node { FIR_NODE() };

/// @name Predicates
/// @{

/// @return `true` if the given node tag represents a type.
/// @see FIR_TYPE_LIST.
FIR_SYMBOL bool fir_node_tag_is_ty(enum fir_node_tag);
/// @return `true` if the given node tag represents a nominal node.
/// @see FIR_NOMINAL_NODE_LIST.
FIR_SYMBOL bool fir_node_tag_is_nominal(enum fir_node_tag);
/// @return `true` if the given node tag represents an integer arithmetic operation.
/// @see FIR_IARITH_OP_LIST.
FIR_SYMBOL bool fir_node_tag_is_iarith_op(enum fir_node_tag);
/// @return `true` if the given node tag represents a floating-point arithmetic operation.
/// @see FIR_FARITH_OP_LIST.
FIR_SYMBOL bool fir_node_tag_is_farith_op(enum fir_node_tag);
/// @return `true` if the given node tag represents an arithmetic operation (integer or floating-point).
FIR_SYMBOL bool fir_node_tag_is_arith_op(enum fir_node_tag);
/// @return `true` if the given node tag represents an integer comparison.
/// @see FIR_ICMP_OP_LIST.
FIR_SYMBOL bool fir_node_tag_is_icmp_op(enum fir_node_tag);
/// @return `true` if the given node tag represents a floating-point comparison.
/// @see FIR_FCMP_OP_LIST.
FIR_SYMBOL bool fir_node_tag_is_fcmp_op(enum fir_node_tag);
/// @return `true` if the given node tag represents a comparison (integer or floating-point).
FIR_SYMBOL bool fir_node_tag_is_cmp_op(enum fir_node_tag);
/// @return `true` if the given node tag represents a bitwise operation.
/// @see FIR_BIT_OP_LIST.
FIR_SYMBOL bool fir_node_tag_is_bit_op(enum fir_node_tag);
/// @return `true` if the given node tag represents a bitshift operation.
/// @see FIR_SHIFT_OP_LIST.
FIR_SYMBOL bool fir_node_tag_is_shift_op(enum fir_node_tag);
/// @return `true` if the given node tag represents a cast.
/// @see FIR_CAST_OP_LIST.
FIR_SYMBOL bool fir_node_tag_is_cast_op(enum fir_node_tag);
/// @return `true` if the given node tag represents an aggregate operation.
/// @see FIR_AGGR_OP_LIST.
FIR_SYMBOL bool fir_node_tag_is_aggr_op(enum fir_node_tag);
/// @return `true` if the given node tag represents a memory operation.
/// @see FIR_MEM_OP_LIST.
FIR_SYMBOL bool fir_node_tag_is_mem_op(enum fir_node_tag);
/// @return `true` if the given node tag represents a control-flow operation.
/// @see FIR_CONTROL_OP_LIST.
FIR_SYMBOL bool fir_node_tag_is_control_op(enum fir_node_tag);

/// @return `true` if the given node tag represents a node that carries floating-point flags.
FIR_SYMBOL bool fir_node_tag_has_fp_flags(enum fir_node_tag);
/// @return `true` if the given node tag represents a node that carries memory flags.
FIR_SYMBOL bool fir_node_tag_has_mem_flags(enum fir_node_tag);
/// @return `true` if the given node tag represents a type with a bitwidth.
FIR_SYMBOL bool fir_node_tag_has_bitwidth(enum fir_node_tag);

/// @return `true` if the given node tag corresponds to a node that can be made external (typically
/// functions and globals).
/// @see fir_node_make_external, fir_node_make_internal.
FIR_SYMBOL bool fir_node_tag_can_be_external(enum fir_node_tag);

/// @see fir_node_tag_is_ty.
FIR_SYMBOL bool fir_node_is_ty(const struct fir_node*);
/// @see fir_node_tag_is_ty.
FIR_SYMBOL bool fir_node_is_nominal(const struct fir_node*);
/// @see fir_node_tag_is_iarith_op.
FIR_SYMBOL bool fir_node_is_iarith_op(const struct fir_node*);
/// @see fir_node_tag_is_farith_op.
FIR_SYMBOL bool fir_node_is_farith_op(const struct fir_node*);
/// @see fir_node_tag_is_arith_op.
FIR_SYMBOL bool fir_node_is_arith_op(const struct fir_node*);
/// @see fir_node_tag_is_icmp_op.
FIR_SYMBOL bool fir_node_is_icmp_op(const struct fir_node*);
/// @see fir_node_tag_is_fcmp_op.
FIR_SYMBOL bool fir_node_is_fcmp_op(const struct fir_node*);
/// @see fir_node_tag_is_cmp_op.
FIR_SYMBOL bool fir_node_is_cmp_op(const struct fir_node*);
/// @see fir_node_tag_is_bit_op.
FIR_SYMBOL bool fir_node_is_bit_op(const struct fir_node*);
/// @see fir_node_tag_is_shift_op.
FIR_SYMBOL bool fir_node_is_shift_op(const struct fir_node*);
/// @see fir_node_tag_is_cast_op.
FIR_SYMBOL bool fir_node_is_cast_op(const struct fir_node*);
/// @see fir_node_tag_is_aggr_op.
FIR_SYMBOL bool fir_node_is_aggr_op(const struct fir_node*);
/// @see fir_node_tag_is_mem_op.
FIR_SYMBOL bool fir_node_is_mem_op(const struct fir_node*);
/// @see fir_node_tag_is_control_op.
FIR_SYMBOL bool fir_node_is_control_op(const struct fir_node*);

/// @see fir_node_tag_has_fp_flags.
FIR_SYMBOL bool fir_node_has_fp_flags(const struct fir_node*);
/// @see fir_node_tag_has_mem_flags.
FIR_SYMBOL bool fir_node_has_mem_flags(const struct fir_node*);
/// @see fir_node_tag_has_bitwidth.
FIR_SYMBOL bool fir_node_has_bitwidth(const struct fir_node*);

/// @return `true` if the given node is an integer constant.
/// @see fir_int_const.
FIR_SYMBOL bool fir_node_is_int_const(const struct fir_node*);
/// @return `true` if the given node is a floating-point constant.
/// @see fir_float_const.
FIR_SYMBOL bool fir_node_is_float_const(const struct fir_node*);
/// @return `true` if the given node is boolean type.
/// @see fir_bool_ty.
FIR_SYMBOL bool fir_node_is_bool_ty(const struct fir_node*);
/// @return `true` if the given node is continuation type.
FIR_SYMBOL bool fir_node_is_cont_ty(const struct fir_node*);
/// @return `true` if the given node is a bitwise NOT.
/// @see fir_not.
FIR_SYMBOL bool fir_node_is_not(const struct fir_node*);
/// @return `true` if the given node is an integer negation.
/// @see fir_ineg.
FIR_SYMBOL bool fir_node_is_ineg(const struct fir_node*);
/// @return `true` if the given node is a floating-point negation.
/// @see fir_fneg.
FIR_SYMBOL bool fir_node_is_fneg(const struct fir_node*);

/// @return `true` if the given node is an integer or floating-point constant equal to 0.
/// @see fir_zero.
FIR_SYMBOL bool fir_node_is_zero(const struct fir_node*);
/// @return `true` if the given node is an integer or floating-point constant equal to 1.
/// @see fir_one.
FIR_SYMBOL bool fir_node_is_one(const struct fir_node*);
/// @return `true` if the given node is an integer constant with all bits set.
/// @see fir_all_ones.
FIR_SYMBOL bool fir_node_is_all_ones(const struct fir_node*);

/// @return `true` if the given node is the unit value (empty tuple).
/// @see fir_unit.
FIR_SYMBOL bool fir_node_is_unit(const struct fir_node*);
/// @return `true` if the given node is the unit type (empty tuple type).
/// @see fir_unit_ty.
FIR_SYMBOL bool fir_node_is_unit_ty(const struct fir_node*);

/// @return `true` if the given node is a boolean selection.
/// @see fir_select.
FIR_SYMBOL bool fir_node_is_select(const struct fir_node*);
/// @return `true` if the given node is a n-ary choice.
/// @see fir_choice.
FIR_SYMBOL bool fir_node_is_choice(const struct fir_node*);
/// @return `true` if the given node is a jump (a call to a continuation).
FIR_SYMBOL bool fir_node_is_jump(const struct fir_node*);
/// @return `true` if the given node is a conditional jump.
/// @see fir_branch.
FIR_SYMBOL bool fir_node_is_branch(const struct fir_node*);
/// @return `true` if the given node is a indexed jump.
/// @see fir_switch.
FIR_SYMBOL bool fir_node_is_switch(const struct fir_node*);

/// @return `true` if the given node is external.
/// External nodes are either imported or exported.
FIR_SYMBOL bool fir_node_is_external(const struct fir_node*);
/// @return `true` if the given node is imported.
/// @see fir_node_is_exported.
FIR_SYMBOL bool fir_node_is_imported(const struct fir_node*);
/// @return `true` if the given node is exported.
/// @see fir_node_is_imported.
FIR_SYMBOL bool fir_node_is_exported(const struct fir_node*);
/// @see fir_node_tag_can_be_external.
FIR_SYMBOL bool fir_node_can_be_external(const struct fir_node*);

/// @}

/// Converts the given node tag to a human-readable string.
FIR_SYMBOL const char* fir_node_tag_to_string(enum fir_node_tag);
/// Returns the module that the given node was created from.
FIR_SYMBOL struct fir_mod* fir_node_mod(const struct fir_node*);
/// Returns the name of the given node, based on its debug information (if any).
FIR_SYMBOL const char* fir_node_name(const struct fir_node*);
/// Builds a unique name for the node. The returned string must be freed.
FIR_SYMBOL char* fir_node_unique_name(const struct fir_node*);
/// Sets the debug information attached to the node.
FIR_SYMBOL void fir_node_set_dbg_info(const struct fir_node*, const struct fir_dbg_info*);
/// Sets the operand of a nominal node.
FIR_SYMBOL void fir_node_set_op(struct fir_node* node, size_t op_index, const struct fir_node* op);

/// Marks a node as external. Not all nodes can be made external.
/// @see fir_node_can_be_external.
FIR_SYMBOL void fir_node_make_external(struct fir_node*);
/// Marks a node as internal. Only valid for nodes that are external already.
/// @see fir_node_make_external.
FIR_SYMBOL void fir_node_make_internal(struct fir_node*);

/// Rebuilds the given _structural_ node with new operands and type into the given module.
/// Constant values and other node-specific data is taken from the original node.
FIR_SYMBOL const struct fir_node* fir_node_rebuild(
    struct fir_mod*,
    enum fir_node_tag tag,
    const union fir_node_data* data,
    const struct fir_node* ty,
    const struct fir_node* const* ops,
    size_t op_count);

/// @name Printing
/// @{

/// Clones the given _nominal_ node with a new type into the given module.
/// Linkage is inherited from the original nominal node.
FIR_SYMBOL struct fir_node* fir_node_clone(
    struct fir_mod*,
    const struct fir_node* nominal_node,
    const struct fir_node* ty);

/// Verbosity levels when printing objects to streams.
enum fir_verbosity {
    FIR_VERBOSITY_COMPACT,   ///< Minimum verbosity level, compact output.
    FIR_VERBOSITY_MEDIUM,    ///< Medium verbosity level, good default.
    FIR_VERBOSITY_HIGH       ///< High verbosity level, for debugging.
};

/// Options passed to @ref fir_node_print and @ref fir_mod_print.
struct fir_print_options {
    const char* tab;              ///< String used as a tabulation character for indentation.
    size_t indent;                ///< Indentation level.
    bool disable_colors;          ///< Disables terminal colors in the output.
    enum fir_verbosity verbosity; ///< Verbosity of the output (when applicable).
};

/// Constructs default print options for the given output stream. This makes sure that colors are
/// disabled for streams that do not support them.
FIR_SYMBOL struct fir_print_options fir_print_options_default(FILE*);

/// Prints a node on the given file with the given indentation level.
FIR_SYMBOL void fir_node_print(FILE*, const struct fir_node*, const struct fir_print_options*);
/// Prints a node on standard output.
FIR_SYMBOL void fir_node_dump(const struct fir_node*);

/// @}

/// @name Aggregates
/// @{

/// Prepends one or more elements in front of the given value or type. If the value (resp. type) is
/// a tuple (resp. tuple type), the elements are added in front of the tuple elements (resp. tuple
/// type elements). If not, a tuple (resp. tuple type) is created containing both the elements and
/// the value (resp. type), in that order.
FIR_SYMBOL const struct fir_node* fir_node_prepend(
    const struct fir_node* node,
    const struct fir_node* const* elems,
    size_t elem_count);

/// Chops one or more elements in front of a tuple type or value of type tuple.
FIR_SYMBOL const struct fir_node* fir_node_chop(const struct fir_node* node, size_t elem_count);

/// @}

/// @name Control-flow and functions
/// @{

/// Returns the first basic-block of a function, or `NULL` if it is not set.
FIR_SYMBOL const struct fir_node* fir_node_func_entry(const struct fir_node*);
/// Returns the return continuation of a function, or `NULL` if it is not set.
FIR_SYMBOL const struct fir_node* fir_node_func_return(const struct fir_node*);
/// Returns the stack frame of this function, or `NULL` if it is not set.
FIR_SYMBOL const struct fir_node* fir_node_func_frame(const struct fir_node*);
/// Returns the memory parameter of the given function or basic-block, or `NULL` if it does not exist.
FIR_SYMBOL const struct fir_node* fir_node_mem_param(const struct fir_node*);
/// Returns a list of jump targets for a jump instruction. The jump targets are listed in the same
/// order as specified by the array generated by a call to @ref fir_branch or @ref fir_switch.
FIR_SYMBOL const struct fir_node* const* fir_node_jump_targets(const struct fir_node*);
/// Returns the number of jump targets for a jump instruction.
FIR_SYMBOL size_t fir_node_jump_target_count(const struct fir_node*);
/// Returns the condition used in a node representing a branch or switch.
/// @see fir_branch, fir_switch.
FIR_SYMBOL const struct fir_node* fir_node_switch_cond(const struct fir_node*);

/// @}

/// @name Uses
/// @{

/// Counts the number of uses in the list.
FIR_SYMBOL size_t fir_use_count(const struct fir_use*);
/// Counts the number of uses in the list, stops when the given count is reached.
/// @note This is typically used to check whether a node is used more than once, twice, ...
FIR_SYMBOL size_t fir_use_count_up_to(const struct fir_use*, size_t max_count);
/// Finds a use matching a user and index in the given list.
/// @return A pointer to the matching use if it exists, otherwise `NULL`.
FIR_SYMBOL const struct fir_use* fir_use_find(
    const struct fir_use*,
    const struct fir_node* user,
    size_t index);

/// @}

/// @name Operand access macros
/// @{

/// Obtains the parameter type of a function type.
#define FIR_FUNC_TY_PARAM(x) (fir_assert_tag((x), FIR_FUNC_TY)->ops[0])
/// Obtains the return type of a function type.
#define FIR_FUNC_TY_RET(x) (fir_assert_tag((x), FIR_FUNC_TY)->ops[1])
/// Obtains the element type at the given index in a tuple type.
#define FIR_TUP_TY_ELEM(x, i) (fir_assert_tag((x), FIR_TUP_TY)->ops[i])
/// Obtains the element type of an array type.
#define FIR_ARRAY_TY_ELEM(x) (fir_assert_tag((x), FIR_ARRAY_TY)->ops[0])
/// Obtains the frame of a local variable.
#define FIR_LOCAL_FRAME(x) (fir_assert_tag((x), FIR_LOCAL)->ops[0])
/// Obtains the initializer of a local variable.
#define FIR_LOCAL_INIT(x) (fir_assert_tag((x), FIR_LOCAL)->ops[1])
/// Obtains the initializer of a global variable.
#define FIR_GLOBAL_INIT(x) (fir_assert_tag((x), FIR_GLOBAL)->ops[0])
/// Obtains the body of a function.
#define FIR_FUNC_BODY(x) (fir_assert_tag((x), FIR_FUNC)->ops[0])
/// Obtains the memory object of a load.
#define FIR_LOAD_MEM(x) (fir_assert_tag((x), FIR_LOAD)->ops[0])
/// Obtains the pointer of a load.
#define FIR_LOAD_PTR(x) (fir_assert_tag((x), FIR_LOAD)->ops[1])
/// Obtains the memory object of a store.
#define FIR_STORE_MEM(x) (fir_assert_tag((x), FIR_STORE)->ops[0])
/// Obtains the pointer of a store.
#define FIR_STORE_PTR(x) (fir_assert_tag((x), FIR_STORE)->ops[1])
/// Obtains the value stored to a pointer by a store.
#define FIR_STORE_VAL(x) (fir_assert_tag((x), FIR_STORE)->ops[2])
/// Obtains the element at the given index in a tuple.
#define FIR_TUP_ARG(x, i) (fir_assert_tag((x), FIR_TUP)->ops[i])
/// Obtains the element at the given index in an array.
#define FIR_ARRAY_ARG(x, i) (fir_assert_tag((x), FIR_ARRAY)->ops[i])
/// Obtains the aggregate that is extracted from.
#define FIR_EXT_AGGR(x) (fir_assert_tag((x), FIR_EXT)->ops[0])
/// Obtains the index of the element extracted from an aggregate.
#define FIR_EXT_INDEX(x) (fir_assert_tag((x), FIR_EXT)->ops[1])
/// Obtains the aggregate that is inserted into.
#define FIR_INS_AGGR(x) (fir_assert_tag((x), FIR_INS)->ops[0])
/// Obtains the index of the element inserted into an aggregate.
#define FIR_INS_INDEX(x) (fir_assert_tag((x), FIR_INS)->ops[1])
/// Obtains the element inserted into an aggregate.
#define FIR_INS_ELEM(x) (fir_assert_tag((x), FIR_INS)->ops[2])
/// Obtains the pointer that is the base of an address calculation.
#define FIR_ADDROF_PTR(x) (fir_assert_tag((x), FIR_ADDROF)->ops[0])
/// Obtains the aggregate type that the address calculation uses.
#define FIR_ADDROF_TY(x) (fir_assert_tag((x), FIR_ADDROF)->ops[1])
/// Obtains the index of the element whose address is computed.
#define FIR_ADDROF_INDEX(x) (fir_assert_tag((x), FIR_ADDROF)->ops[2])
/// Obtains the left-hand-side of an arithmetic operation.
#define FIR_ARITH_OP_LEFT(x) (fir_assert_kind((x), fir_node_is_arith_op)->ops[0])
/// Obtains the right-hand-side of an arithmetic operation.
#define FIR_ARITH_OP_RIGHT(x) (fir_assert_kind((x), fir_node_is_arith_op)->ops[1])
/// Obtains the left-hand-side of a comparison.
#define FIR_CMP_OP_LEFT(x) (fir_assert_kind((x), fir_node_is_cmp_op)->ops[0])
/// Obtains the right-hand-side of a comparison.
#define FIR_CMP_OP_RIGHT(x) (fir_assert_kind((x), fir_node_is_cmp_op)->ops[1])
/// Obtains the left-hand-side of a bitwise operation.
#define FIR_BIT_OP_LEFT(x) (fir_assert_kind((x), fir_node_is_bit_op)->ops[0])
/// Obtains the right-hand-side of a bitwise operation.
#define FIR_BIT_OP_RIGHT(x) (fir_assert_kind((x), fir_node_is_bit_op)->ops[1])
/// Obtains the value whose bits are shifted in a bit shift operation.
#define FIR_SHIFT_OP_VAL(x) (fir_assert_kind((x), fir_node_is_bit_op)->ops[0])
/// Obtains the number of bits that are shifted in a bit shift operation.
#define FIR_SHIFT_OP_AMOUNT(x) (fir_assert_kind((x), fir_node_is_bit_op)->ops[1])
/// Obtains the value which is casted into another type by a cast operation.
#define FIR_CAST_OP_ARG(x) (fir_assert_kind((x), fir_node_is_cast_op)->ops[0])
/// Obtains the function to which a parameter belongs.
#define FIR_PARAM_FUNC(x) (fir_assert_tag((x), FIR_PARAM)->ops[0])
/// Obtains the basic-block that is specified by a start instruction.
#define FIR_START_FUNC(x) (fir_assert_tag((x), FIR_START)->ops[0])
/// Obtains the callee of a call instruction.
#define FIR_CALL_CALLEE(x) (fir_assert_tag((x), FIR_CALL)->ops[0])
/// Obtains the argument of a call instruction.
#define FIR_CALL_ARG(x) (fir_assert_tag((x), FIR_CALL)->ops[1])

/// @cond include_hidden
#ifdef NDEBUG
#define fir_assert_tag(x, t) (x)
#define fir_assert_kind(x, k) (x)
#else
#define fir_assert_tag(x, t) fir_assert_tag_debug((x), t)
#define fir_assert_kind(x, k) fir_assert_kind_debug((x), k)
#endif

FIR_SYMBOL const struct fir_node* fir_assert_tag_debug(const struct fir_node*, enum fir_node_tag);
FIR_SYMBOL const struct fir_node* fir_assert_kind_debug(const struct fir_node*, bool (*kind)(const struct fir_node*));
/// @endcond

/// @}

#endif
