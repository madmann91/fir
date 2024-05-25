#include "fir/block.h"
#include "fir/node.h"
#include "fir/module.h"

#include "datatypes.h"

#include <assert.h>

static inline struct fir_block make_block(
    struct fir_node* block,
    struct fir_node* func,
    const struct fir_node* mem,
    bool is_wired)
{
    return (struct fir_block) {
        .func = func,
        .block = block,
        .mem = mem ? mem : fir_ext_mem(NULL, fir_param(block)),
        .is_merge_block = mem == NULL,
        .is_terminated = false,
        .is_wired = is_wired
    };
}

static inline void jump(struct fir_block* from, const struct fir_node* target) {
    assert(!from->is_terminated);
    from->is_terminated = true;
    fir_node_set_op(from->block, 0, target);
}

const struct fir_node* fir_block_start(struct fir_block* entry, struct fir_node* func) {
    struct fir_mod* mod = fir_node_mod(func);
    const struct fir_node* ret_ty = fir_cont_ty(FIR_FUNC_TY_RET(func->ty));
    const struct fir_node* start_ty = fir_tup_ty(mod,
        (const struct fir_node*[]) { fir_frame_ty(mod), ret_ty }, 2);
    const struct fir_node* mem = fir_ext_mem(NULL, fir_param(func));
    assert(mem);
    *entry = make_block(fir_cont(start_ty), func, mem, true);
    entry->is_merge_block = false;
    fir_node_set_op(func, 0, fir_start(entry->block));
    return fir_node_chop(fir_param(func), 1);
}

struct fir_block fir_block_create_merge(struct fir_node* func) {
    return make_block(fir_cont(fir_mem_ty(fir_node_mod(func))), func, NULL, false);
}

void fir_block_branch(
    struct fir_block* from,
    const struct fir_node* cond,
    struct fir_block* block_true,
    struct fir_block* block_false)
{
    fir_block_switch(from, cond, (struct fir_block*[]) { block_true, block_false }, 2);
}

void fir_block_switch(
    struct fir_block* from,
    const struct fir_node* index,
    struct fir_block** targets,
    size_t target_count)
{
    struct fir_mod* mod = fir_node_mod(index);
    struct small_node_vec target_blocks;
    small_node_vec_init(&target_blocks);
    for (size_t i = 0; i < target_count; ++i) {
        *targets[i]  = make_block(fir_cont(fir_unit_ty(mod)), from->func, from->mem, true);
        small_node_vec_push(&target_blocks, (const struct fir_node*[]) { targets[i]->block });
    }
    jump(from, fir_switch(NULL, index, fir_unit(mod), target_blocks.elems, target_count));
    small_node_vec_destroy(&target_blocks);
}

void fir_block_loop(struct fir_block* from, struct fir_block* continue_block) {
    struct fir_mod* mod = fir_node_mod(from->block);
    *continue_block = make_block(fir_cont(fir_mem_ty(mod)), from->func, NULL, true);
    jump(from, fir_call(NULL, continue_block->block, from->mem));
}

void fir_block_jump(struct fir_block* from, struct fir_block* target) {
    assert(target->is_merge_block);
    assert(from->func == target->func);
    if (!from->is_terminated) {
        target->is_wired = true;
        jump(from, fir_call(NULL, target->block, from->mem));
    }
}

void fir_block_return(struct fir_block* from, const struct fir_node* ret_val) {
    if (!from->is_terminated) {
        jump(from, fir_call(NULL,
            fir_node_func_return(from->func),
            fir_node_prepend(ret_val, &from->mem, 1)));
    }
}

const struct fir_node* fir_block_call(
    struct fir_block* block,
    const struct fir_node* callee,
    const struct fir_node* arg)
{
    const struct fir_node* ret_val = fir_call(NULL, callee, fir_node_prepend(arg, &block->mem, 1));
    block->mem = fir_ext_at(NULL, ret_val, 0);
    assert(block->mem->ty->tag == FIR_MEM_TY);
    return fir_node_chop(ret_val, 1);
}

const struct fir_node* fir_block_load(
    struct fir_block* block,
    enum fir_mem_flags mem_flags,
    const struct fir_node* ptr,
    const struct fir_node* ty)
{
    assert(!block->is_terminated);
    const struct fir_node* tup = fir_load(mem_flags, NULL, block->mem, ptr, ty);
    block->mem = fir_ext_at(tup, NULL, 0);
    return fir_ext_at(tup, NULL, 1);
}

void fir_block_store(
    struct fir_block* block,
    enum fir_mem_flags mem_flags,
    const struct fir_node* ptr,
    const struct fir_node* val)
{
    assert(!block->is_terminated);
    block->mem = fir_store(mem_flags, NULL, block->mem, ptr, val);
}
