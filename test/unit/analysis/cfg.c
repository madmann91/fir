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
    struct fir_block merge_block;

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
    //            |
    //            v
    //          entry
    //          |   |
    //          v   v
    //       first second
    //          |   |
    //          v   v
    //           ret
    //            |
    //            v
    //          sink

    struct graph_node* source = cfg.graph.source;
    struct graph_node* sink = cfg.graph.sink;
    REQUIRE(!source->ins);
    REQUIRE(!sink->outs);
    REQUIRE(source->outs);
    REQUIRE(sink->ins);

    struct graph_node* entry = source->outs->to;
    REQUIRE(!source->outs->next_out);

    REQUIRE(entry->outs);
    REQUIRE(entry->outs->next_out);
    REQUIRE(!entry->outs->next_out->next_out);
    struct graph_node* first  = entry->outs->to;
    struct graph_node* second = entry->outs->next_out->to;

    REQUIRE(first->outs);
    REQUIRE(second->outs);
    REQUIRE(!first->outs->next_out);
    REQUIRE(!second->outs->next_out);
    REQUIRE(first->outs->to == second->outs->to);

    struct graph_node* ret = first->outs->to;
    REQUIRE(ret->outs);
    REQUIRE(!ret->outs->next_out);
    REQUIRE(ret->outs->to == sink);

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
    fir_block_store(&entry, i, n);
    const struct fir_node* p = fir_block_alloc(&entry, int32_ty);
    fir_block_store(&entry, p, fir_one(int32_ty));

    struct fir_block loop;
    struct fir_block done;
    fir_block_loop(&entry, &loop, &done);

    struct fir_block is_zero;
    struct fir_block is_non_zero;
    struct fir_block merge_block;
    const struct fir_node* cur_i = fir_block_load(&loop, int32_ty, i);
    const struct fir_node* cond = fir_icmp_op(FIR_ICMPEQ, cur_i, fir_zero(int32_ty));
    fir_block_branch(&loop, cond, &is_zero, &is_non_zero, &merge_block);
    fir_block_jump(&is_zero, &done);

    const struct fir_node* q = fir_iarith_op(FIR_IMUL, fir_block_load(&is_non_zero, int32_ty, p), x);
    const struct fir_node* j = fir_iarith_op(FIR_ISUB, fir_block_load(&is_non_zero, int32_ty, i), fir_one(int32_ty));
    fir_block_store(&is_non_zero, p, q);
    fir_block_store(&is_non_zero, i, j);
    fir_block_jump(&is_non_zero, &loop);

    const struct fir_node* k = fir_block_load(&done, int32_ty, i);
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
    //          entry
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

    struct graph_node* entry = source->outs->to;
    REQUIRE(!source->outs->next_out);

    REQUIRE(entry->outs);
    REQUIRE(!entry->outs->next_out);
    struct graph_node* loop  = entry->outs->to;

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
    struct graph_node* ret = done->outs->to;

    REQUIRE(ret->outs);
    REQUIRE(!ret->outs->next_out);
    REQUIRE(ret->outs->to == sink);

    scope_destroy(&scope);
    cfg_destroy(&cfg);
    fir_mod_destroy(mod);
}
