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
/// they are stored uniquely into a hash map, identified by their tag, type, data, and operands.
/// Such nodes are called _structural nodes_ (identified by their structure) and are completely
/// immutable. Some nodes are mutable, like functions or globals, and are referred to as _nominal
/// nodes_ (identified by their name). This means, for instance, that calling @ref fir_int_ty twice
/// will return the same pointer, whereas calling @ref fir_func twice will result in two different
/// functions. The API reflects this by returning `const` nodes for structural nodes, and
/// non-`const` nodes for nominal ones. The operands of a nominal nodes must be set via @ref
/// fir_node_set_op, or otherwise the users will not be updated.

/// @struct fir_mod
/// IR module.
struct fir_mod;

struct fir_dbg_info_pool;

/// Creates a module with the given name.
FIR_SYMBOL struct fir_mod* fir_mod_create(const char* name);
/// Destroys the given module. This releases memory holding all the nodes in the module.
FIR_SYMBOL void fir_mod_destroy(struct fir_mod*);

/// Returns the module name.
FIR_SYMBOL const char* fir_mod_name(const struct fir_mod*);
/// Sets the module name.
FIR_SYMBOL void fir_mod_set_name(struct fir_mod*, const char* name);
/// Sets the module name given a string with the given length.
FIR_SYMBOL void fir_mod_set_name_with_length(struct fir_mod*, const char* name, size_t name_len);

/// Cleans up the module. This performs dead code elimination on the entire module,
/// and updates the uses of each node correspondingly. The complexity of this function is linear in
/// the number of live nodes.
/// @warning The memory used by dead nodes is reclaimed: Any pointer to dead code will no longer
/// point to valid memory after a call to this function. Types remain valid and are _not_ reclaimed.
FIR_SYMBOL void fir_mod_cleanup(struct fir_mod*);

/// Returns the functions of the module.
FIR_SYMBOL struct fir_node* const* fir_mod_funcs(const struct fir_mod*);
/// Returns the global variables of the module.
FIR_SYMBOL struct fir_node* const* fir_mod_globals(const struct fir_mod*);
/// Returns the number of functions in the module.
FIR_SYMBOL size_t fir_mod_func_count(const struct fir_mod*);
/// Returns the number of global variables in the module.
FIR_SYMBOL size_t fir_mod_global_count(const struct fir_mod*);

/// @name Printing
/// @{

/// Options passed to @ref fir_mod_print.
struct fir_mod_print_options {
    size_t indent;                ///< Indentation level.
    const char* tab;              ///< String used as a tabulation character.
    bool disable_colors;          ///< Disables terminal colors in the output.
    enum fir_verbosity verbosity; ///< Verbosity of the output (when applicable).
};

/// Prints the given module on the given stream.
FIR_SYMBOL void fir_mod_print(
    FILE* file,
    const struct fir_mod*,
    const struct fir_mod_print_options*);

/// Prints the given module on the standard output.
FIR_SYMBOL void fir_mod_dump(const struct fir_mod*);

/// @}

/// @name Parsing
/// @{

/// Input passed to @ref fir_mod_parse.
struct fir_parse_input {
    const char* file_name; ///< Name of the file being parsed, appearing in error messages.
    const char* file_data; ///< File data, which must be `NULL`-terminated.
    size_t file_size;      ///< File size, excluding the `NULL` terminator.
    FILE* error_log;       ///< Where errors will be reported, or `NULL` to disable error reporting.

    /// Where to store debug information, or `NULL` to discard debug information.
    struct fir_dbg_info_pool* dbg_pool;
};

/// Parses a module from the given input.
/// @return `true` on success, otherwise `false`.
FIR_SYMBOL bool fir_mod_parse(struct fir_mod*, const struct fir_parse_input* input);

/// @}

/// @name Types
/// @{

/// Type of memory tokens used to keep track of memory effects.
FIR_SYMBOL const struct fir_node* fir_mem_ty(struct fir_mod*);
/// Type of frame tokens used to keep track of function stack frames.
FIR_SYMBOL const struct fir_node* fir_frame_ty(struct fir_mod*);
/// Type of control objects used to keep track of control-dependencies.
FIR_SYMBOL const struct fir_node* fir_ctrl_ty(struct fir_mod*);
/// Special type used as the return type of continuations.
FIR_SYMBOL const struct fir_node* fir_noret_ty(struct fir_mod*);
/// Generic pointer type.
FIR_SYMBOL const struct fir_node* fir_ptr_ty(struct fir_mod*);

/// Fixed-size array type.
FIR_SYMBOL const struct fir_node* fir_array_ty(const struct fir_node* elem_ty, size_t size);
/// Dynamic-size array type.
FIR_SYMBOL const struct fir_node* fir_dynarray_ty(const struct fir_node* elem_ty);

/// Integer type. Supported bitwidths are <= 64.
FIR_SYMBOL const struct fir_node* fir_int_ty(struct fir_mod*, size_t bitwidth);
/// Floating-point type. Supported bitwidths are 16, 32, and 64.
FIR_SYMBOL const struct fir_node* fir_float_ty(struct fir_mod*, size_t bitwidth);
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

/// Function type carrying a memory object. This prepends a memory object type to the parameter
/// type list and return type list.
FIR_SYMBOL const struct fir_node* fir_mem_func_ty(
    const struct fir_node* param_ty,
    const struct fir_node* ret_ty);

/// Builds a continuation type with the given parameter type.
/// Shortcut for `func_ty(param_ty, noret_ty)`
FIR_SYMBOL const struct fir_node* fir_cont_ty(const struct fir_node* param_ty);

/// Continuation type carrying a memory object. This prepends a memory object type to the parameter
/// type list.
FIR_SYMBOL const struct fir_node* fir_mem_cont_ty(const struct fir_node* param_ty);

/// @}

/// @name Nominal nodes
/// @{

/// Creates a function or continuation. Expects a function type as argument.
FIR_SYMBOL struct fir_node* fir_func(const struct fir_node* func_ty);

/// Creates a global variable, typed as a pointer.
FIR_SYMBOL struct fir_node* fir_global(struct fir_mod*);

/// Allocates a piece of data local to the current function.
FIR_SYMBOL struct fir_node* fir_local(
    const struct fir_node* frame,
    const struct fir_node* init);

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

/// Boolean constant.
FIR_SYMBOL const struct fir_node* fir_bool_const(struct fir_mod*, bool bool_val);
/// Integer constant.
FIR_SYMBOL const struct fir_node* fir_int_const(const struct fir_node* ty, fir_int_val int_val);
/// Floating-point constant.
FIR_SYMBOL const struct fir_node* fir_float_const(const struct fir_node* ty, fir_float_val float_val);

/// Integer or floating-point constant equal to zero.
FIR_SYMBOL const struct fir_node* fir_zero(const struct fir_node* ty);
/// Integer or floating-point constant equal to one.
FIR_SYMBOL const struct fir_node* fir_one(const struct fir_node* ty);
/// Integer constant with all bits set.
FIR_SYMBOL const struct fir_node* fir_all_ones(const struct fir_node* ty);

/// @}

/// @name Arithmetic or bitwise operations, comparisons, and casts
/// @{

/// Integer arithmetic operations. @see FIR_IARITH_OP_LIST.
FIR_SYMBOL const struct fir_node* fir_iarith_op(
    enum fir_node_tag tag,
    const struct fir_node* ctrl,
    const struct fir_node* left,
    const struct fir_node* right);

/// Floating-point arithmetic operations. @see FIR_FARITH_OP_LIST.
FIR_SYMBOL const struct fir_node* fir_farith_op(
    enum fir_node_tag tag,
    enum fir_fp_flags,
    const struct fir_node* ctrl,
    const struct fir_node* left,
    const struct fir_node* right);

/// Integer comparisons. @see FIR_ICMP_OP_LIST.
FIR_SYMBOL const struct fir_node* fir_icmp_op(
    enum fir_node_tag tag,
    const struct fir_node* ctrl,
    const struct fir_node* left,
    const struct fir_node* right);

/// Floating-point comparisons. @see FIR_FCMP_OP_LIST.
FIR_SYMBOL const struct fir_node* fir_fcmp_op(
    enum fir_node_tag tag,
    const struct fir_node* ctrl,
    const struct fir_node* left,
    const struct fir_node* right);

/// Bitwise operations. @see FIR_BIT_OP_LIST.
FIR_SYMBOL const struct fir_node* fir_bit_op(
    enum fir_node_tag tag,
    const struct fir_node* ctrl,
    const struct fir_node* left,
    const struct fir_node* right);

/// Integer bitshift operations. @see FIR_SHIFT_OP_LIST.
FIR_SYMBOL const struct fir_node* fir_shift_op(
    enum fir_node_tag tag,
    const struct fir_node* ctrl,
    const struct fir_node* val,
    const struct fir_node* amount);

/// Casts between primitive (integer and floating-point) types. @see FIR_CAST_OP_LIST.
FIR_SYMBOL const struct fir_node* fir_cast_op(
    enum fir_node_tag tag,
    const struct fir_node* ctrl,
    const struct fir_node* ty,
    const struct fir_node* arg);

/// Constructs a bitwise not, using a XOR instruction.
FIR_SYMBOL const struct fir_node* fir_not(
    const struct fir_node* ctrl,
    const struct fir_node* arg);

/// Constructs an integer negation, using a subtraction instruction.
FIR_SYMBOL const struct fir_node* fir_ineg(
    const struct fir_node* ctrl,
    const struct fir_node* arg);

/// Constructs a floating-point negation, using a subtraction instruction.
FIR_SYMBOL const struct fir_node* fir_fneg(
    enum fir_fp_flags,
    const struct fir_node* ctrl,
    const struct fir_node* arg);

/// @}

/// @name Aggregate instructions
/// @{

/// Creates a tuple with the given elements.
FIR_SYMBOL const struct fir_node* fir_tup(
    struct fir_mod* mod,
    const struct fir_node* ctrl,
    const struct fir_node* const* elems,
    size_t elem_count);

/// Returns a tuple without any elements.
FIR_SYMBOL const struct fir_node* fir_unit(struct fir_mod*);

/// Creates a fixed-size array.
FIR_SYMBOL const struct fir_node* fir_array(
    const struct fir_node* ctrl,
    const struct fir_node* ty,
    const struct fir_node* const* elems);

/// Extracts an element from an existing tuple or array.
FIR_SYMBOL const struct fir_node* fir_ext(
    const struct fir_node* ctrl,
    const struct fir_node* aggr,
    const struct fir_node* index);

/// Extracts an element from an existing tuple or array at the given constant index.
/// Shortcut for `ext(aggr, const[index])`.
FIR_SYMBOL const struct fir_node* fir_ext_at(
    const struct fir_node* ctrl,
    const struct fir_node* aggr,
    size_t index);

/// Extracts the memory object from a value, if any.
/// @return The memory object contained in the value, if it exists, or `NULL` otherwise.
FIR_SYMBOL const struct fir_node* fir_ext_mem(
    const struct fir_node* ctrl,
    const struct fir_node* val);

/// Inserts an element into an existing tuple or array.
FIR_SYMBOL const struct fir_node* fir_ins(
    const struct fir_node* ctrl,
    const struct fir_node* aggr,
    const struct fir_node* index,
    const struct fir_node* elem);

/// Inserts an element into an existing tuple or array at the given constant index.
FIR_SYMBOL const struct fir_node* fir_ins_at(
    const struct fir_node* ctrl,
    const struct fir_node* aggr,
    size_t index,
    const struct fir_node* elem);

/// Inserts a new memory object into an existing value that is or contains a memory object.
FIR_SYMBOL const struct fir_node* fir_ins_mem(
    const struct fir_node* ctrl,
    const struct fir_node* val,
    const struct fir_node* mem);

/// Obtains the address of an element of a tuple or array, given the address of the tuple or array.
FIR_SYMBOL const struct fir_node* fir_addrof(
    const struct fir_node* ctrl,
    const struct fir_node* ptr,
    const struct fir_node* aggr_ty,
    const struct fir_node* index);

/// Obtains the address of the element with the given index in a tuple or array, given the address
/// of the tuple or array.
FIR_SYMBOL const struct fir_node* fir_addrof_at(
    const struct fir_node* ctrl,
    const struct fir_node* ptr,
    const struct fir_node* aggr_ty,
    size_t index);

/// Builds a selection out of an extract and an array.
/// Shortcut for `ext(array(when_false, when_true), cond)`.
FIR_SYMBOL const struct fir_node* fir_select(
    const struct fir_node* ctrl,
    const struct fir_node* cond,
    const struct fir_node* when_true,
    const struct fir_node* when_false);

/// Builds a n-ary selection out of an extract and an array.
/// Shortcut for `ext(array(x1, ..., xn), index)`.
FIR_SYMBOL const struct fir_node* fir_choice(
    const struct fir_node* ctrl,
    const struct fir_node* index,
    const struct fir_node* const* elems,
    size_t elem_count);

/// @}

/// @name Memory operations
/// @{

/// Loads the data located at the given address. The block
FIR_SYMBOL const struct fir_node* fir_load(
    enum fir_mem_flags flags,
    const struct fir_node* ctrl,
    const struct fir_node* mem,
    const struct fir_node* ptr,
    const struct fir_node* ty);

/// Stores data at the given address.
FIR_SYMBOL const struct fir_node* fir_store(
    enum fir_mem_flags flags,
    const struct fir_node* ctrl,
    const struct fir_node* mem,
    const struct fir_node* ptr,
    const struct fir_node* val);

/// @}

/// @name Control-flow and functions
/// @{

/// Calls a regular function, or jumps to a continuation.
FIR_SYMBOL const struct fir_node* fir_call(
    const struct fir_node* ctrl,
    const struct fir_node* callee,
    const struct fir_node* arg);

/// Branches to either continuation based on the condition.
/// Shortcut for `call(ext(array(jump_false, jump_true), cond), arg)`.
/// @see fir_select, fir_call.
FIR_SYMBOL const struct fir_node* fir_branch(
    const struct fir_node* ctrl,
    const struct fir_node* cond,
    const struct fir_node* arg,
    const struct fir_node* jump_true,
    const struct fir_node* jump_false);

/// Branches to a continuation based on the given index.
/// This is a more general version of @ref fir_branch, as it supports more than 2 jump targets.
/// Shortcut for `call(ext(array(target1, ..., targetn), index), arg)`.
/// @see fir_choice, fir_call.
FIR_SYMBOL const struct fir_node* fir_switch(
    const struct fir_node* ctrl,
    const struct fir_node* cond,
    const struct fir_node* arg,
    const struct fir_node* const* targets,
    size_t target_count);

/// Obtains the parameter of a function.
FIR_SYMBOL const struct fir_node* fir_param(const struct fir_node* func);

/// Obtains the control-dependence object of a function or basic-block.
FIR_SYMBOL const struct fir_node* fir_ctrl(const struct fir_node* func);

/// Enters the first basic-block of a function. This node takes the first block of the function as
/// an operand, which must have a type of the form:
///
///     func_ty(tup_ty(frame_ty, func_ty(T, noret_ty)), noret_ty)
///
/// where `T` is the return type of the function. The resulting `start` node has type `T`, which
/// makes it possible to use it as the operand of the function which is being started. The first
/// basic-block takes two parameters: A parameter of type `frame_ty` that can be used to allocate
/// local variables in the function, and second parameter of type `func_ty(T, noret_ty)` which is
/// the function to call in order to return from the function, also known as the
/// _return continuation_.
FIR_SYMBOL const struct fir_node* fir_start(const struct fir_node* block);

/// @}

#endif
