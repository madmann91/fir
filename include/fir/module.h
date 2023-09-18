#ifndef FIR_MODULE_H
#define FIR_MODULE_H

#include "fir/platform.h"
#include "fir/fp_flags.h"
#include "fir/node.h"

#include <stdint.h>
#include <stddef.h>

/// @file
///
/// The module acts as a container for all IR nodes. Most nodes are hash-consed, which means that
/// they are stored uniquely into a hash map, identified by their tag, type, data, and operands. Such
/// nodes are called _structural nodes_ (identified by their structure) and are completely immutable.
/// Some nodes are mutable, like functions or globals, and are referred to as _nominal nodes_
/// (identified by their name). This means, for instance, that calling `fir_int_ty(mod, 32)` twice
/// will return the same pointer, whereas calling `fir_func(ty)` twice will result in two different
/// functions. The API reflects this by returning `const` nodes for structural nodes, and
/// non-`const` nodes for nominal ones. The operands of a nominal nodes must be set via `fir_set_op`,
/// or otherwise the users will not be updated.

/// @struct fir_mod
/// IR module.
struct fir_mod;

/// Creates a module with the given name.
FIR_SYMBOL struct fir_mod* fir_mod_create(const char* name);
/// Destroys the given module. This releases memory holding all the nodes in the module.
FIR_SYMBOL void fir_mod_destroy(struct fir_mod*);

/// Returns the functions of the module.
FIR_SYMBOL struct fir_node** fir_mod_funcs(struct fir_mod*);
/// Returns the global variables of the module.
FIR_SYMBOL struct fir_node** fir_mod_globals(struct fir_mod*);
/// Returns the number of functions in the module.
FIR_SYMBOL size_t fir_mod_func_count(struct fir_mod*);
/// Returns the number of global variables in the module.
FIR_SYMBOL size_t fir_mod_global_count(struct fir_mod*);

/// Prints the given module in the given stream.
FIR_SYMBOL void fir_mod_print(FILE*, const struct fir_mod*);
/// Prints the given module on the standard output.
FIR_SYMBOL void fir_mod_dump(const struct fir_mod*);

/// Parses a module from the given NULL-terminated file data.
/// Returns true on success, otherwise false. Errors are reported on the standard error stream.
FIR_SYMBOL bool fir_parse_mod(
    const char* file_name,
    const char* file_data,
    size_t file_size,
    struct fir_mod*);

/// @name Types
/// @{

/// Type of memory tokens used to keep track of memory effects.
FIR_SYMBOL const struct fir_node* fir_mem_ty(struct fir_mod*);
/// Type of error tokens used to keep track of exceptions.
FIR_SYMBOL const struct fir_node* fir_err_ty(struct fir_mod*);
/// Special type used as the return type of continuations.
FIR_SYMBOL const struct fir_node* fir_noret_ty(struct fir_mod*);
/// Generic pointer type.
FIR_SYMBOL const struct fir_node* fir_ptr_ty(struct fir_mod*);

/// Fixed-size array type.
FIR_SYMBOL const struct fir_node* fir_array_ty(const struct fir_node* elem_ty, size_t size);
/// Dynamic-size array type.
FIR_SYMBOL const struct fir_node* fir_dynarray_ty(const struct fir_node* elem_ty);

/// Integer type. Supported bitwidths are <= 64.
FIR_SYMBOL const struct fir_node* fir_int_ty(struct fir_mod*, uint32_t bitwidth);
/// Floating-point type. Supported bitwidths are 16, 32, and 64.
FIR_SYMBOL const struct fir_node* fir_float_ty(struct fir_mod*, uint32_t bitwidth);
/// Boolean type (integer type with bitwidth 1).
FIR_SYMBOL const struct fir_node* fir_bool_ty(struct fir_mod*);

/// Tuple type.
FIR_SYMBOL const struct fir_node* fir_tup_ty(
    struct fir_mod*,
    const struct fir_node* const* elems,
    size_t elem_count);

/// Tuple type without any element.
FIR_SYMBOL const struct fir_node* fir_unit_ty(struct fir_mod*);

/// Function (or continuation) type.
FIR_SYMBOL const struct fir_node* fir_func_ty(
    const struct fir_node* param_ty,
    const struct fir_node* ret_ty);

/// Builds a continuation type with the given parameter type.
/// Shortcut for `func_ty(param_ty, noret_ty)`
FIR_SYMBOL const struct fir_node* fir_cont_ty(const struct fir_node* param_ty);

/// @}

/// @name Nominal nodes
/// @{

/// Creates a function or continuation. Expects a function type as argument.
FIR_SYMBOL struct fir_node* fir_func(const struct fir_node* func_ty);

/// Creates a global variable, initialized with a bottom value of the given type.
/// Global variables are just pointers to some global memory and are thus typed as pointers.
FIR_SYMBOL struct fir_node* fir_global(const struct fir_node* ty);

/// Creates a continuation with the given parameter type.
/// Shortcut for `func(func_ty(param_ty, noret_ty))`.
FIR_SYMBOL struct fir_node* fir_cont(const struct fir_node* param_ty);

/// @}

/// @name Constants
/// @{

/// Bottom value, representing the absence of a value (empty set).
FIR_SYMBOL const struct fir_node* fir_top(const struct fir_node* ty);
/// Top value, representing all possible values (full set).
FIR_SYMBOL const struct fir_node* fir_bot(const struct fir_node* ty);

/// Integer constant.
FIR_SYMBOL const struct fir_node* fir_int_const(const struct fir_node* ty, uint64_t int_val);
/// Floating-point constant.
FIR_SYMBOL const struct fir_node* fir_float_const(const struct fir_node* ty, double float_val);

/// @}

/// @name Arithmetic or bitwise operations, comparisons, and casts
/// @{

/// Integer arithmetic operations (excluding division and remainder). @see FIR_IARITH_OP_LIST.
FIR_SYMBOL const struct fir_node* fir_iarith_op(
    enum fir_node_tag tag,
    const struct fir_node* left,
    const struct fir_node* right);

/// Floating-point arithmetic operations (excluding division and remainder). @see FIR_FARITH_OP_LIST.
FIR_SYMBOL const struct fir_node* fir_farith_op(
    enum fir_node_tag tag,
    enum fir_fp_flags,
    const struct fir_node* left,
    const struct fir_node* right);

/// Integer divisions and remainder operations. @see FIR_IDIV_OP_LIST.
FIR_SYMBOL const struct fir_node* fir_idiv_op(
    enum fir_node_tag tag,
    const struct fir_node* err,
    const struct fir_node* left,
    const struct fir_node* right);

/// Floating-point divisions and remainder operations. @see FIR_FDIV_OP_LIST.
FIR_SYMBOL const struct fir_node* fir_fdiv_op(
    enum fir_node_tag tag,
    enum fir_fp_flags,
    const struct fir_node* err,
    const struct fir_node* left,
    const struct fir_node* right);

/// Integer comparisons. @see FIR_ICMP_OP_LIST.
FIR_SYMBOL const struct fir_node* fir_icmp_op(
    enum fir_node_tag tag,
    const struct fir_node* left,
    const struct fir_node* right);

/// Floating-point comparisons. @see FIR_FCMP_OP_LIST.
FIR_SYMBOL const struct fir_node* fir_fcmp_op(
    enum fir_node_tag tag,
    const struct fir_node* left,
    const struct fir_node* right);

/// Bitwise operations. @see FIR_BIT_OP_LIST.
FIR_SYMBOL const struct fir_node* fir_bit_op(
    enum fir_node_tag tag,
    const struct fir_node* left,
    const struct fir_node* right);

/// Casts between primitive (integer and floating-point) types. @see FIR_CAST_OP_LIST.
FIR_SYMBOL const struct fir_node* fir_cast_op(
    enum fir_node_tag tag,
    const struct fir_node* ty,
    const struct fir_node* arg);

/// @}

/// @name Aggregate instructions
/// @{

/// Creates a tuple with the given elements.
FIR_SYMBOL const struct fir_node* fir_tup(
    struct fir_mod* mod,
    const struct fir_node* const* elems,
    size_t elem_count);

/// Returns a tuple without any elements.
FIR_SYMBOL const struct fir_node* fir_unit(struct fir_mod*);

/// Creates a fixed-size array.
FIR_SYMBOL const struct fir_node* fir_array(
    const struct fir_node* ty,
    const struct fir_node* const* elems);

/// Extracts an element from an existing tuple or array.
FIR_SYMBOL const struct fir_node* fir_ext(
    const struct fir_node* aggr,
    const struct fir_node* index);

/// Inserts an element into an existing tuple or array.
FIR_SYMBOL const struct fir_node* fir_ins(
    const struct fir_node* aggr,
    const struct fir_node* index,
    const struct fir_node* elem);

/// Obtains the address of an element of a tuple or array, given the address of the tuple or array.
FIR_SYMBOL const struct fir_node* fir_addrof(
    const struct fir_node* ptr,
    const struct fir_node* aggr_ty,
    const struct fir_node* index);

/// Builds a selection out of an extract and an array.
/// Shortcut for `ext(array(when_false, when_true), cond)`.
FIR_SYMBOL const struct fir_node* fir_select(
    const struct fir_node* cond,
    const struct fir_node* when_true,
    const struct fir_node* when_false);

/// @}

/// @name Control-flow and functions
/// @{

/// Calls a regular function, or jumps to a continuation.
FIR_SYMBOL const struct fir_node* fir_call(
    const struct fir_node* callee,
    const struct fir_node* arg);

/// Branches to either continuation based on the condition.
/// Shortcut for `call(select(cond, jump_true, jump_false), tup())`.
FIR_SYMBOL const struct fir_node* fir_branch(
    const struct fir_node* cond,
    const struct fir_node* jump_true,
    const struct fir_node* jump_false);

/// Obtains the parameter of a function.
FIR_SYMBOL const struct fir_node* fir_param(const struct fir_node* func);
/// Enters the first basic-block of a function.
FIR_SYMBOL const struct fir_node* fir_start(const struct fir_node* block);

/// @}

#endif
