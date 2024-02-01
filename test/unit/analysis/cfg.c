#include "macros.h"

#include "analysis/scope.h"
#include "analysis/cfg.h"

#include <fir/module.h>
#include <fir/block.h>
#include <fir/node.h>

static inline struct fir_node* build_rec_pow(struct fir_mod* mod) {
    const struct fir_node* int32_ty = fir_int_ty(mod, 32);
    const struct fir_node* mem_ty = fir_mem_ty(mod);
    const struct fir_node* param_ty = fir_tup_ty(mod,
        (const struct fir_node*[]) { mem_ty, int32_ty, int32_ty }, 3);
    const struct fir_node* ret_ty = fir_tup_ty(mod,
        (const struct fir_node*[]) { mem_ty, int32_ty }, 2);

    struct fir_node* pow = fir_func(fir_func_ty(param_ty, ret_ty));
    struct fir_block entry;
    const struct fir_node* param = fir_block_start(&entry, pow);
    const struct fir_node* x = fir_ext_at(param, 0);
    const struct fir_node* n = fir_ext_at(param, 1);

    // if (n == 0)
    //   goto is_zero;
    // else
    //   goto is_non_zero;
    struct fir_block is_zero;
    struct fir_block is_non_zero;
    struct fir_block merge_block = fir_block_merge(pow);

    const struct fir_node* cond = fir_icmp_op(FIR_ICMPEQ, n, fir_zero(int32_ty));
    fir_block_branch(&entry, cond, &is_zero, &is_non_zero, &merge_block);

    // is_zero:
    //   return 1
    fir_block_return(&is_zero, fir_one(x->ty));

    // is_non_zero:
    //   return x * pow(x, n - 1)
    const struct fir_node* n_minus_1 = fir_iarith_op(FIR_ISUB, n, fir_one(int32_ty));
    const struct fir_node* x_n_minus_1 = fir_tup(mod, (const struct fir_node*[]) { x, n_minus_1 }, 2);
    const struct fir_node* pow_x_n_minus_1 = fir_block_call(&is_non_zero, pow, x_n_minus_1);
    fir_block_return(&is_non_zero, fir_iarith_op(FIR_IMUL, x, pow_x_n_minus_1));

    return pow;
}

TEST(cfg_rec_pow) {
    struct fir_mod* mod = fir_mod_create("module");
    struct fir_node* rec_pow = build_rec_pow(mod);
    struct scope scope = scope_create(rec_pow);
    struct cfg cfg = cfg_create(&scope);

    // The CFG must look like this:
    //          source
    //          |   |
    //          v   v
    //       first second
    //          |   |
    //          v   v
    //          sink

    struct graph_node* source = cfg.graph.source;
    struct graph_node* sink = cfg.graph.sink;
    REQUIRE(!source->ins);
    REQUIRE(!sink->outs);

    REQUIRE(source->outs);
    REQUIRE(source->outs->next_out);
    REQUIRE(!source->outs->next_out->next_out);

    REQUIRE(sink->ins);
    REQUIRE(sink->ins->next_in);
    REQUIRE(!sink->ins->next_in->next_in);

    struct graph_node* first  = source->outs->to;
    struct graph_node* second = source->outs->next_out->to;

    REQUIRE(
        (sink->ins->from == first  && sink->ins->next_in->from == second) ||
        (sink->ins->from == second && sink->ins->next_in->from == first));

    REQUIRE(first->outs);
    REQUIRE(second->outs);
    REQUIRE(!first->outs->next_out);
    REQUIRE(!second->outs->next_out);
    REQUIRE(first->outs->to == second->outs->to);
    REQUIRE(first->outs->to == sink);

    REQUIRE(cfg_dom_tree_node(first)->idom == source);
    REQUIRE(cfg_dom_tree_node(second)->idom == source);
    REQUIRE(cfg_dom_tree_node(sink)->idom == source);

    REQUIRE(cfg_dom_tree_node(source)->depth == 1);
    REQUIRE(cfg_dom_tree_node(first)->depth == 2);
    REQUIRE(cfg_dom_tree_node(second)->depth == 2);
    REQUIRE(cfg_dom_tree_node(sink)->depth == 2);

    REQUIRE(cfg_post_dom_tree_node(source)->idom == sink);
    REQUIRE(cfg_post_dom_tree_node(first)->idom == sink);
    REQUIRE(cfg_post_dom_tree_node(second)->idom == sink);

    REQUIRE(cfg_post_dom_tree_node(source)->depth == 2);
    REQUIRE(cfg_post_dom_tree_node(first)->depth == 2);
    REQUIRE(cfg_post_dom_tree_node(second)->depth == 2);
    REQUIRE(cfg_post_dom_tree_node(sink)->depth == 1);

    REQUIRE(cfg_loop_tree_node(source)->parent == source);
    REQUIRE(cfg_loop_tree_node(first)->parent == source);
    REQUIRE(cfg_loop_tree_node(second)->parent == source);
    REQUIRE(cfg_loop_tree_node(sink)->parent == source);

    REQUIRE(cfg_loop_tree_node(source)->depth == 1);
    REQUIRE(cfg_loop_tree_node(first)->depth == 2);
    REQUIRE(cfg_loop_tree_node(second)->depth == 2);
    REQUIRE(cfg_loop_tree_node(sink)->depth == 2);

    REQUIRE(cfg_loop_tree_node(source)->loop_depth == 0);
    REQUIRE(cfg_loop_tree_node(first)->loop_depth == 0);
    REQUIRE(cfg_loop_tree_node(second)->loop_depth == 0);
    REQUIRE(cfg_loop_tree_node(sink)->loop_depth == 0);

    scope_destroy(&scope);
    cfg_destroy(&cfg);
    fir_mod_destroy(mod);
}

static inline struct fir_node* build_iter_pow(struct fir_mod* mod) {
    const struct fir_node* int32_ty = fir_int_ty(mod, 32);
    const struct fir_node* mem_ty = fir_mem_ty(mod);
    const struct fir_node* param_ty = fir_tup_ty(mod,
        (const struct fir_node*[]) { mem_ty, int32_ty, int32_ty }, 3);
    const struct fir_node* ret_ty = fir_tup_ty(mod,
        (const struct fir_node*[]) { mem_ty, int32_ty }, 2);

    struct fir_node* pow = fir_func(fir_func_ty(param_ty, ret_ty));
    struct fir_block entry;
    const struct fir_node* param = fir_block_start(&entry, pow);
    const struct fir_node* x = fir_ext_at(param, 0);
    const struct fir_node* n = fir_ext_at(param, 1);

    // i = n;
    // p = 1;
    //
    // loop:
    //   if (i == 0)
    //     goto is_zero;
    //   else
    //     goto is_non_zero;
    //
    // is_zero:
    //   goto done;
    //
    // is_non_zero:
    //   p *= x;
    //   i--;
    //   goto loop;
    //
    // done:
    //   return i

    const struct fir_node* i = fir_block_alloc(&entry, int32_ty);
    fir_block_store(&entry, i, n, FIR_MEM_NON_NULL);
    const struct fir_node* p = fir_block_alloc(&entry, int32_ty);
    fir_block_store(&entry, p, fir_one(int32_ty), FIR_MEM_NON_NULL);

    struct fir_block loop;
    struct fir_block done = fir_block_merge(pow);
    fir_block_loop(&entry, &loop, &done);

    struct fir_block is_zero;
    struct fir_block is_non_zero;
    struct fir_block merge_block = fir_block_merge(pow);
    const struct fir_node* cur_i = fir_block_load(&loop, i, int32_ty, FIR_MEM_NON_NULL);
    const struct fir_node* cond = fir_icmp_op(FIR_ICMPEQ, cur_i, fir_zero(int32_ty));
    fir_block_branch(&loop, cond, &is_zero, &is_non_zero, &merge_block);
    fir_block_jump(&is_zero, &done);

    const struct fir_node* q = fir_iarith_op(FIR_IMUL, fir_block_load(&is_non_zero, p, int32_ty, FIR_MEM_NON_NULL), x);
    const struct fir_node* j = fir_iarith_op(FIR_ISUB, fir_block_load(&is_non_zero, i, int32_ty, FIR_MEM_NON_NULL), fir_one(int32_ty));
    fir_block_store(&is_non_zero, p, q, FIR_MEM_NON_NULL);
    fir_block_store(&is_non_zero, i, j, FIR_MEM_NON_NULL);
    fir_block_jump(&is_non_zero, &loop);

    const struct fir_node* k = fir_block_load(&done, i, int32_ty, FIR_MEM_NON_NULL);
    fir_block_return(&done, k);

    return pow;
}

TEST(cfg_iter_pow) {
    struct fir_mod* mod = fir_mod_create("module");
    struct fir_node* rec_pow = build_iter_pow(mod);
    struct scope scope = scope_create(rec_pow);
    struct cfg cfg = cfg_create(&scope);

    // The CFG must look like this:
    //          source
    //            |
    //            v
    //          loop <-----------
    //          |  |            |
    //          v  v            |
    //    is_zero  is_non_zero --
    //       |
    //       v
    //     done
    //       |
    //       v
    //     sink

    struct graph_node* source = cfg.graph.source;
    struct graph_node* sink = cfg.graph.sink;
    REQUIRE(!source->ins);
    REQUIRE(!sink->outs);
    REQUIRE(source->outs);
    REQUIRE(sink->ins);

    REQUIRE(!source->outs->next_out);
    struct graph_node* loop  = source->outs->to;

    REQUIRE(loop->outs);
    REQUIRE(loop->outs->next_out);
    REQUIRE(!loop->outs->next_out->next_out);
    struct graph_node* is_zero     = loop->outs->to;
    struct graph_node* is_non_zero = loop->outs->next_out->to;

    if (is_zero->outs && is_zero->outs->to == loop) {
        is_zero     = loop->outs->next_out->to;
        is_non_zero = loop->outs->to;
    }

    REQUIRE(is_non_zero->outs);
    REQUIRE(!is_non_zero->outs->next_out);
    REQUIRE(is_non_zero->outs->to == loop);

    REQUIRE(is_zero->outs);
    REQUIRE(!is_zero->outs->next_out);
    struct graph_node* done = is_zero->outs->to;

    REQUIRE(done->outs);
    REQUIRE(!done->outs->next_out);
    REQUIRE(done->outs->to == sink);

    REQUIRE(cfg_dom_tree_node(loop)->idom == source);
    REQUIRE(cfg_dom_tree_node(is_zero)->idom == loop);
    REQUIRE(cfg_dom_tree_node(is_non_zero)->idom == loop);
    REQUIRE(cfg_dom_tree_node(done)->idom == is_zero);
    REQUIRE(cfg_dom_tree_node(sink)->idom == done);

    REQUIRE(cfg_dom_tree_node(source)->depth == 1);
    REQUIRE(cfg_dom_tree_node(loop)->depth == 2);
    REQUIRE(cfg_dom_tree_node(is_zero)->depth == 3);
    REQUIRE(cfg_dom_tree_node(is_non_zero)->depth == 3);
    REQUIRE(cfg_dom_tree_node(done)->depth == 4);
    REQUIRE(cfg_dom_tree_node(sink)->depth == 5);

    REQUIRE(cfg_post_dom_tree_node(source)->idom == loop);
    REQUIRE(cfg_post_dom_tree_node(loop)->idom == is_zero);
    REQUIRE(cfg_post_dom_tree_node(is_zero)->idom == done);
    REQUIRE(cfg_post_dom_tree_node(is_non_zero)->idom == loop);
    REQUIRE(cfg_post_dom_tree_node(done)->idom == sink);

    REQUIRE(cfg_post_dom_tree_node(source)->depth == 5);
    REQUIRE(cfg_post_dom_tree_node(loop)->depth == 4);
    REQUIRE(cfg_post_dom_tree_node(is_zero)->depth == 3);
    REQUIRE(cfg_post_dom_tree_node(is_non_zero)->depth == 5);
    REQUIRE(cfg_post_dom_tree_node(done)->depth == 2);
    REQUIRE(cfg_post_dom_tree_node(sink)->depth == 1);

    REQUIRE(cfg_loop_tree_node(source)->parent == source);
    REQUIRE(cfg_loop_tree_node(loop)->parent == source);
    REQUIRE(cfg_loop_tree_node(is_zero)->parent == source);
    REQUIRE(cfg_loop_tree_node(is_non_zero)->parent == loop);
    REQUIRE(cfg_loop_tree_node(done)->parent == source);

    REQUIRE(cfg_loop_tree_node(source)->depth == 1);
    REQUIRE(cfg_loop_tree_node(loop)->depth == 2);
    REQUIRE(cfg_loop_tree_node(is_zero)->depth == 2);
    REQUIRE(cfg_loop_tree_node(is_non_zero)->depth == 3);
    REQUIRE(cfg_loop_tree_node(done)->depth == 2);

    REQUIRE(cfg_loop_tree_node(source)->loop_depth == 0);
    REQUIRE(cfg_loop_tree_node(loop)->loop_depth == 1);
    REQUIRE(cfg_loop_tree_node(is_zero)->loop_depth == 0);
    REQUIRE(cfg_loop_tree_node(is_non_zero)->loop_depth == 1);
    REQUIRE(cfg_loop_tree_node(done)->loop_depth == 0);

    scope_destroy(&scope);
    cfg_destroy(&cfg);
    fir_mod_destroy(mod);
}
