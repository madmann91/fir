#ifndef FIR_MODULE_H
#define FIR_MODULE_H

#include "fir/fp_flags.h"

#include <stdint.h>
#include <stddef.h>

/// \file
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

/// \struct fir_mod
/// IR module.
struct fir_mod;
struct fir_node;

struct fir_mod* fir_mod_create(const char* name);
void fir_mod_destroy(struct fir_mod*);

struct fir_node** fir_mod_funcs(struct fir_mod*);
struct fir_node** fir_mod_globals(struct fir_mod*);
size_t fir_mod_func_count(struct fir_mod*);
size_t fir_mod_global_count(struct fir_mod*);

//-------------------------------------------------------------------------------------------------
/// \name Types
//-------------------------------------------------------------------------------------------------

/// \{

const struct fir_node* fir_mem_ty(struct fir_mod*);
const struct fir_node* fir_err_ty(struct fir_mod*);
const struct fir_node* fir_noret_ty(struct fir_mod*);
const struct fir_node* fir_ptr_ty(struct fir_mod*);
const struct fir_node* fir_array_ty(const struct fir_node* elem_ty, size_t size);
const struct fir_node* fir_dynarray_ty(const struct fir_node* elem_ty);

const struct fir_node* fir_int_ty(struct fir_mod*, uint32_t bitwidth);
const struct fir_node* fir_float_ty(struct fir_mod*, uint32_t bitwidth);

const struct fir_node* fir_tup_ty(
    struct fir_mod*,
    const struct fir_node* const* args,
    size_t arg_count);

const struct fir_node* fir_func_ty(
    const struct fir_node* param_ty,
    const struct fir_node* ret_ty);

/// \}

//-------------------------------------------------------------------------------------------------
/// \name Nominal nodes
//-------------------------------------------------------------------------------------------------

/// \{

struct fir_node* fir_func(const struct fir_node* func_ty);
struct fir_node* fir_block(const struct fir_node* param_ty);
struct fir_node* fir_global(const struct fir_node* ty);

/// \}

//-------------------------------------------------------------------------------------------------
/// \name Constants
//-------------------------------------------------------------------------------------------------

/// \{

const struct fir_node* fir_top(const struct fir_node* ty);
const struct fir_node* fir_bot(const struct fir_node* ty);

const struct fir_node* fir_int_const(const struct fir_node* ty, uint64_t int_val);
const struct fir_node* fir_float_const(const struct fir_node* ty, double float_val);

/// \}

//-------------------------------------------------------------------------------------------------
/// \name Arithmetic or bitwise operations, comparisons, and casts
//-------------------------------------------------------------------------------------------------

/// \{

const struct fir_node* fir_iarith_op(
    uint32_t tag,
    const struct fir_node* left,
    const struct fir_node* right);

const struct fir_node* fir_farith_op(
    uint32_t tag,
    enum fir_fp_flags,
    const struct fir_node* left,
    const struct fir_node* right);

const struct fir_node* fir_idiv_op(
    uint32_t tag,
    const struct fir_node* err,
    const struct fir_node* left,
    const struct fir_node* right);

const struct fir_node* fir_fdiv_op(
    uint32_t tag,
    enum fir_fp_flags,
    const struct fir_node* err,
    const struct fir_node* left,
    const struct fir_node* right);

const struct fir_node* fir_icmp_op(
    uint32_t tag,
    const struct fir_node* left,
    const struct fir_node* right);

const struct fir_node* fir_fcmp_op(
    uint32_t tag,
    const struct fir_node* left,
    const struct fir_node* right);

const struct fir_node* fir_bit_op(
    uint32_t tag,
    const struct fir_node* left,
    const struct fir_node* right);

const struct fir_node* fir_cast_op(
    uint32_t tag,
    const struct fir_node* ty,
    const struct fir_node* arg);

/// \}

//-------------------------------------------------------------------------------------------------
/// \name Aggregate operations
//-------------------------------------------------------------------------------------------------

/// \{

const struct fir_node* fir_tup(
    const struct fir_node*const* elems,
    size_t elem_count);

const struct fir_node* fir_array(
    const struct fir_node* ty,
    const struct fir_node** elems,
    size_t elem_count);

const struct fir_node* fir_ext(
    const struct fir_node* aggr,
    const struct fir_node* index);

const struct fir_node* fir_ins(
    const struct fir_node* aggr,
    const struct fir_node* index,
    const struct fir_node* elem);

const struct fir_node* fir_addrof(
    const struct fir_node* ptr,
    const struct fir_node* aggr_ty,
    const struct fir_node* index);

/// \}

//-------------------------------------------------------------------------------------------------
/// \name Control-flow and functions
//-------------------------------------------------------------------------------------------------

/// \{

const struct fir_node* fir_call(
    const struct fir_node* callee,
    const struct fir_node* arg);

const struct fir_node* fir_param(const struct fir_node* func);
const struct fir_node* fir_start(const struct fir_node* block);

/// \}

#endif
