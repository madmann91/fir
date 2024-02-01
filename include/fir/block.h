#ifndef FIR_BLOCK_H
#define FIR_BLOCK_H

#include "fir/platform.h"
#include "fir/node.h"

#include <stdbool.h>
#include <stddef.h>

/// @file
///
/// Basic-block helpers for IR emission from high-level languages. When building IR from a
/// high-level language, it is useful to have the concept of loops, conditional branches, and
/// basic-blocks. The functions in this file simplify the creation of such control structures, and
/// contain checks to verify that basic-blocks are wired correctly. Additionally, those functions
/// track the state of memory, allowing to load and store values without having to pass the current
/// memory object.

struct fir_node;

/// A basic-block under construction.
struct fir_block {
    struct fir_node* func;       ///< Function containing the basic-block.
    struct fir_node* block;      ///< Function representing the basic-block (non-returning).
    const struct fir_node* mem;  ///< Current memory object in the basic-block.
    bool is_merge_block : 1;     ///< `true` if and only if the basic-block may have multiple predecessors.
    bool is_terminated : 1;      ///< `true` if and only if the basic-block jumps somewhere.
    bool is_wired : 1;           ///< `true` if and only if the basic-block has a predecessor.
};

/// @name Control-flow
/// @{

/// Produces the first basic-block of the given function.
/// @param[out] entry The first basic block of the function.
/// @param[in]  func  The function in which the first basic-block is placed.
/// @return The parameter of the function, with the memory object removed.
FIR_SYMBOL const struct fir_node* fir_block_start(struct fir_block* entry, struct fir_node* func);

/// Creates a merge block in the given function. This merge block can be used to merge branches from
/// if statements or as a loop exit.
FIR_SYMBOL struct fir_block fir_block_merge(struct fir_node* func);

/// Conditional jump on one of two target blocks, depending on a condition.
/// @param[in] from The basic-block to branch from.
/// @param[in] cond The condition that selects which branch to take.
/// @param[out] block_true The block that will be jumped to if the condition is true.
/// @param[out] block_false The block that will be jumped to if the condition is false.
/// @param[in] merge_block The block to merge control-flow back to.
/// @see fir_block_switch.
FIR_SYMBOL void fir_block_branch(
    struct fir_block* from,
    const struct fir_node* cond,
    struct fir_block* block_true,
    struct fir_block* block_false,
    const struct fir_block* merge_block);

/// Switch statement based on the given index.
/// @param[in] from The basic-block to branch from.
/// @param[in] index The index that selects which block to jump to.
/// @param[out] targets The blocks that will be jumped to depending on the index.
/// @param[in] target_count The number of target blocks.
/// @param[in] merge_block The block to merge control-flow back to.
/// @see fir_block_branch.
FIR_SYMBOL void fir_block_switch(
    struct fir_block* from,
    const struct fir_node* index,
    struct fir_block** targets,
    size_t target_count,
    const struct fir_block* merge_block);

/// Starts an (infinite) loop from the given block.
/// The loop can still be exited by jumping to the merge block from the loop's body.
/// @param[in] from The basic-block to branch from.
/// @param[out] continue_block The block that corresponds to the beginning of the loop.
/// @param[in] break_block The block to merge control-flow back to.
FIR_SYMBOL void fir_block_loop(
    struct fir_block* from,
    struct fir_block* continue_block,
    const struct fir_block* break_block);

/// Jumps between two basic-blocks, if the block is not already terminated.
/// @param[in] from The basic-block to jump from.
/// @param[in] target The basic-block to jump to (must be a merge block).
FIR_SYMBOL void fir_block_jump(struct fir_block* from, struct fir_block* target);

/// Returns from the enclosing function, if the block is not already terminated.
FIR_SYMBOL void fir_block_return(struct fir_block*, const struct fir_node* ret_val);

/// Calls a function with side-effects from the given block.
FIR_SYMBOL const struct fir_node* fir_block_call(
    struct fir_block*,
    const struct fir_node* callee,
    const struct fir_node* arg);

/// @}

/// @name Memory operations
/// @{

/// Allocates a value in the given block.
FIR_SYMBOL const struct fir_node* fir_block_alloc(struct fir_block*, const struct fir_node* ty);

/// Loads a value at the given address, in the given block.
FIR_SYMBOL const struct fir_node* fir_block_load(
    struct fir_block*,
    const struct fir_node* ptr,
    const struct fir_node* ty,
    enum fir_mem_flags mem_flags);

/// Stores a value at the given address, in the given block.
FIR_SYMBOL void fir_block_store(
    struct fir_block*,
    const struct fir_node* ptr,
    const struct fir_node* val,
    enum fir_mem_flags mem_flags);

/// @}

#endif
