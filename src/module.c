#include "fir/module.h"
#include "fir/node.h"

#include "datatypes.h"

#include <overture/set.h>
#include <overture/bits.h>
#include <overture/vec.h>
#include <overture/mem.h>
#include <overture/hash.h>

#include <stdlib.h>
#include <math.h>

#define SMALL_NODE_OP_COUNT 8

struct small_node {
    FIR_NODE(SMALL_NODE_OP_COUNT)
};

typedef int64_t signed_int_val;
static_assert(sizeof(signed_int_val) == sizeof(fir_int_val));

static uint32_t hash_node_data(uint32_t h, const struct fir_node* node) {
    if (fir_node_has_mem_flags(node))
        h = hash_uint32(h, node->data.mem_flags);
    else if (fir_node_has_fp_flags(node))
        h = hash_uint32(h, node->data.fp_flags);
    else if (node->tag == FIR_ARRAY_TY)
        h = hash_uint64(h, node->data.array_dim);
    else if (node->tag == FIR_CONST && node->ty->tag == FIR_INT_TY)
        h = hash_uint64(h, node->data.int_val);
    else if (node->tag == FIR_CONST && node->ty->tag == FIR_FLOAT_TY)
        h = hash_uint64(h, double_to_bits(node->data.float_val));
    else if (fir_node_has_bitwidth(node))
        h = hash_uint32(h, node->data.bitwidth);
    return h;
}

static uint32_t hash_node(uint32_t h, const struct fir_node* const* node_ptr) {
    const struct fir_node* node = *node_ptr;
    h = hash_uint32(h, node->tag);
    if (!fir_node_is_ty(node))
        h = hash_uint64(h, node->ty->id);
    h = hash_node_data(h, node);
    h = hash_uint64(h, node->op_count);
    if (node->ctrl)
        h = hash_uint64(h, node->ctrl->id);
    for (size_t i = 0; i < node->op_count; ++i)
        h = hash_uint64(h, node->ops[i]->id);
    return h;
}

static inline bool has_same_node_data(const struct fir_node* node, const struct fir_node* other) {
    if (fir_node_has_mem_flags(node))
        return node->data.mem_flags == other->data.mem_flags;
    else if (fir_node_has_fp_flags(node))
        return node->data.fp_flags == other->data.fp_flags;
    else if (node->tag == FIR_CONST && node->ty->tag == FIR_INT_TY)
        return node->data.int_val == other->data.int_val;
    else if (node->tag == FIR_CONST && node->ty->tag == FIR_FLOAT_TY)
        return double_to_bits(node->data.float_val) == double_to_bits(other->data.float_val);
    else if (node->tag == FIR_ARRAY_TY)
        return node->data.array_dim == other->data.array_dim;
    else if (fir_node_has_bitwidth(node))
        return node->data.bitwidth == other->data.bitwidth;
    return true;
}

static inline bool is_node_equal(
    const struct fir_node* const* node_ptr,
    const struct fir_node* const* other_ptr)
{
    const struct fir_node* node  = *node_ptr;
    const struct fir_node* other = *other_ptr;
    if (node->tag != other->tag || node->op_count != other->op_count)
        return false;
    if (!fir_node_is_ty(node) && node->ty != other->ty)
        return false;
    if (node->ctrl != other->ctrl)
        return false;
    if (!has_same_node_data(node, other))
        return false;
    for (size_t i = 0; i < node->op_count; ++i) {
        if (node->ops[i] != other->ops[i])
            return false;
    }
    return true;
}

SET_DEFINE(internal_node_set, const struct fir_node*, hash_node, is_node_equal, PRIVATE)
VEC_DEFINE(nominal_node_vec, struct fir_node*, PRIVATE)

struct fir_mod {
    char* name;
    uint64_t cur_id;
    struct nominal_node_vec funcs;
    struct nominal_node_vec globals;
    struct nominal_node_vec locals;
    struct internal_node_set nodes;
    struct node_set external_nodes;
    const struct fir_node* mem_ty;
    const struct fir_node* frame_ty;
    const struct fir_node* ctrl_ty;
    const struct fir_node* noret_ty;
    const struct fir_node* ptr_ty;
    const struct fir_node* unit_ty;
    const struct fir_node* unit;
    const struct fir_node* bool_ty;
    const struct fir_node* index_ty;
    struct fir_use* free_uses;
};

static struct fir_use* alloc_use(struct fir_mod* mod, const struct fir_use* use) {
    struct fir_use* alloced_use = mod->free_uses;
    if (alloced_use)
        mod->free_uses = (struct fir_use*)mod->free_uses->next;
    else
        alloced_use = xmalloc(sizeof(struct fir_use));
    memcpy(alloced_use, use, sizeof(struct fir_use));
    return alloced_use;
}

static void record_use(const struct fir_node* user, size_t i) {
    assert(user->op_count > i);
    struct fir_mod* mod = fir_node_mod(user);
    struct fir_node* used = (struct fir_node*)user->ops[i];
    assert(!fir_node_is_ty(used));
    assert(!fir_node_is_ty(user));
    used->uses = alloc_use(mod, &(struct fir_use) {
        .user = user,
        .index = i,
        .next = used->uses
    });
}

static void forget_use(const struct fir_node* user, size_t i) {
    assert(user->op_count > i);
    struct fir_mod* mod = fir_node_mod(user);
    struct fir_node* used = (struct fir_node*)user->ops[i];
    assert(!fir_node_is_ty(used));
    assert(!fir_node_is_ty(user));
    const struct fir_use** prev = (const struct fir_use**)&used->uses;
    for (struct fir_use* use = (struct fir_use*)used->uses; use; prev = &use->next, use = (struct fir_use*)use->next) {
        if (use->user == user && use->index == i) {
            *prev = use->next;
            use->next = mod->free_uses;
            mod->free_uses = use;
            return;
        }
    }
    assert(false && "trying to remove non-existing use");
}

static void free_uses(struct fir_use* uses) {
    while (uses) {
        struct fir_use* next = (struct fir_use*)uses->next;
        free(uses);
        uses = next;
    }
}

static struct fir_node* alloc_node(size_t op_count) {
    return xcalloc(1, sizeof(struct fir_node) + sizeof(struct fir_node*) * op_count);
}

static void free_node(struct fir_node* node) {
    free_uses((struct fir_use*)node->uses);
    free(node);
}

static inline bool has_side_effect(const struct fir_node* node) {
    switch (node->tag) {
        case FIR_CALL:
            return
                node->ty->tag == FIR_NORET_TY ||
                FIR_CALL_CALLEE(node)->tag != FIR_FUNC ||
                (FIR_CALL_CALLEE(node)->data.func_flags & FIR_FUNC_PURE) == 0;
        case FIR_STORE:
            return true;
        case FIR_LOAD:
            return
                (node->data.mem_flags & FIR_MEM_NON_NULL) != 0 &&
                (node->data.mem_flags & FIR_MEM_VOLATILE) == 0;
        case FIR_SDIV:
        case FIR_UDIV:
        case FIR_SREM:
        case FIR_UREM:
            // It is fine to speculate a division by a constant that is not zero
            return
                FIR_ARITH_OP_RIGHT(node)->tag == FIR_CONST &&
                !fir_node_is_zero(FIR_ARITH_OP_RIGHT(node));
        default:
            return false;
    }
}

[[nodiscard]]
static inline enum fir_node_props compute_props(const struct fir_node* node) {
    enum fir_node_props props = 0;
    if (node->tag != FIR_PARAM)
        props |= FIR_PROP_INVARIANT;
    if (!has_side_effect(node))
        props |= FIR_PROP_SPECULATABLE;
    for (size_t i = 0; i < node->op_count; ++i) {
        if (fir_node_is_nominal(node->ops[i]))
            continue;
        if (!(node->ops[i]->props & FIR_PROP_INVARIANT))
            props &= ~FIR_PROP_INVARIANT;
        if (!(node->ops[i]->props & FIR_PROP_SPECULATABLE))
            props &= ~FIR_PROP_SPECULATABLE;
    }
    return props;
}

static inline const struct fir_node* insert_node(struct fir_mod* mod, const struct fir_node* node) {
    assert(!node->ctrl || node->ctrl->ty->tag == FIR_CTRL_TY);
    assert(!fir_node_tag_is_nominal(node->tag));
    assert(fir_node_mod(node) == mod);
    assert(node->uses == NULL);

#ifndef NDEBUG
    for (size_t i = 0; i < node->op_count; ++i)
        assert(fir_node_mod(node->ops[i]) == mod);
#endif

    const struct fir_node* const* found = internal_node_set_find(&mod->nodes, &node);
    if (found)
        return *found;
    struct fir_node* new_node = alloc_node(node->op_count);
    memcpy(new_node, node, sizeof(struct fir_node) + sizeof(struct fir_node*) * node->op_count);
    if (!fir_node_is_ty(node)) {
        for (size_t i = 0; i < node->op_count; ++i) {
            if (!fir_node_is_ty(node->ops[i]))
                record_use(new_node, i);
        }
    }
    new_node->props = compute_props(new_node);
    new_node->id = mod->cur_id++;

    [[maybe_unused]] bool was_inserted = internal_node_set_insert(&mod->nodes, (const struct fir_node* const*)&new_node);
    assert(was_inserted);
    return new_node;
}

struct fir_mod* fir_mod_create(const char* name) {
    struct fir_mod* mod = xcalloc(1, sizeof(struct fir_mod));
    mod->name = strdup(name);
    mod->cur_id = 0;
    mod->nodes   = internal_node_set_create();
    mod->external_nodes = node_set_create();
    mod->mem_ty   = insert_node(mod, &(struct fir_node) { .tag = FIR_MEM_TY,   .mod = mod });
    mod->frame_ty = insert_node(mod, &(struct fir_node) { .tag = FIR_FRAME_TY, .mod = mod });
    mod->ctrl_ty  = insert_node(mod, &(struct fir_node) { .tag = FIR_CTRL_TY,  .mod = mod });
    mod->noret_ty = insert_node(mod, &(struct fir_node) { .tag = FIR_NORET_TY, .mod = mod });
    mod->ptr_ty   = insert_node(mod, &(struct fir_node) { .tag = FIR_PTR_TY,   .mod = mod });
    mod->unit_ty  = insert_node(mod, &(struct fir_node) { .tag = FIR_TUP_TY,   .mod = mod });
    mod->unit     = insert_node(mod, &(struct fir_node) { .tag = FIR_TUP,      .ty = mod->unit_ty });
    mod->bool_ty  = fir_int_ty(mod, 1);
    mod->index_ty = fir_int_ty(mod, 64);
    return mod;
}

void fir_mod_destroy(struct fir_mod* mod) {
    free(mod->name);
    SET_FOREACH(struct fir_node*, node_ptr, mod->nodes) {
        free_node((struct fir_node*)*node_ptr);
    }
    VEC_FOREACH(struct fir_node*, func_ptr, mod->funcs) {
        free_node((struct fir_node*)*func_ptr);
    }
    VEC_FOREACH(struct fir_node*, global_ptr, mod->globals) {
        free_node((struct fir_node*)*global_ptr);
    }
    VEC_FOREACH(struct fir_node*, local_ptr, mod->locals) {
        free_node((struct fir_node*)*local_ptr);
    }
    node_set_destroy(&mod->external_nodes);
    internal_node_set_destroy(&mod->nodes);
    nominal_node_vec_destroy(&mod->funcs);
    nominal_node_vec_destroy(&mod->globals);
    nominal_node_vec_destroy(&mod->locals);
    free_uses(mod->free_uses);
    free(mod);
}

const char* fir_mod_name(const struct fir_mod* mod) {
    return mod->name;
}

void fir_mod_set_name(struct fir_mod* mod, const char* name) {
    free(mod->name);
    mod->name = strdup(name);
}

void fir_mod_set_name_with_length(struct fir_mod* mod, const char* name, size_t name_len) {
    free(mod->name);
    mod->name = strndup(name, name_len);
}

static void visit_live_node(
    const struct fir_node* node,
    struct node_vec* stack,
    struct node_set* visited_nodes)
{
    node_vec_push(stack, &node);
    while (stack->elem_count > 0) {
        const struct fir_node* top = *node_vec_pop(stack);
        if (!node_set_insert(visited_nodes, &top))
            continue;

        if (top->ctrl)
            node_vec_push(stack, &top->ctrl);
        for (size_t i = 0; i < top->op_count; ++i) {
            if (top->ops[i])
                node_vec_push(stack, &top->ops[i]);
        }
    }
}

static struct node_set collect_live_nodes(struct fir_mod* mod) {
    struct node_set live_nodes = node_set_create();
    struct node_vec stack = node_vec_create();
    VEC_FOREACH(struct fir_node*, func_ptr, mod->funcs) {
        if (fir_node_is_exported(*func_ptr))
            visit_live_node(*func_ptr, &stack, &live_nodes);
    }
    VEC_FOREACH(struct fir_node*, global_ptr, mod->globals) {
        if (fir_node_is_exported(*global_ptr))
            visit_live_node(*global_ptr, &stack, &live_nodes);
    }
    node_vec_destroy(&stack);
    return live_nodes;
}

static void fix_uses(const struct fir_node* node, const struct node_set* live_nodes) {
    struct fir_mod* mod = fir_node_mod(node);
    struct fir_use* use = (struct fir_use*)node->uses;
    const struct fir_use** prev = &((struct fir_node*)node)->uses;
    while (true) {
        while (use && !node_set_find(live_nodes, &use->user)) {
            struct fir_use* next = (struct fir_use*)use->next;
            use->next = mod->free_uses;
            mod->free_uses = use;
            use = next;
        }
        *prev = use;
        if (!use)
            break;
        prev = &use->next;
        use = (struct fir_use*)use->next;
    }
}

static void fix_nominal_nodes_uses(
    struct nominal_node_vec* nodes,
    const struct node_set* live_nodes)
{
    VEC_FOREACH(struct fir_node*, node, *nodes) {
        if (!node_set_find(live_nodes, (const struct fir_node* const*)node))
            continue;
        fix_uses(*node, live_nodes);
    }
}

static inline void cleanup_nominal_nodes(
    struct nominal_node_vec* nodes,
    const struct node_set* live_nodes)
{
    size_t node_count = 0;
    for (size_t i = 0; i < nodes->elem_count; ++i) {
        if (node_set_find(live_nodes, (const struct fir_node*const*)&nodes->elems[i]))
            nodes->elems[node_count++] = nodes->elems[i];
        else
            free_node(nodes->elems[i]);
    }
    nominal_node_vec_resize(nodes, node_count);
}

void fir_mod_cleanup(struct fir_mod* mod) {
    struct node_set live_nodes = collect_live_nodes(mod);
    SET_FOREACH(const struct fir_node*, node_ptr, mod->nodes) {
        if (fir_node_is_ty(*node_ptr) || !node_set_find(&live_nodes, node_ptr))
            continue;
        fix_uses(*node_ptr, &live_nodes);
    }

    fix_nominal_nodes_uses(&mod->funcs, &live_nodes);
    fix_nominal_nodes_uses(&mod->globals, &live_nodes);
    fix_nominal_nodes_uses(&mod->locals, &live_nodes);

    struct node_vec dead_nodes = node_vec_create();
    SET_FOREACH(const struct fir_node*, node_ptr, mod->nodes) {
        if (!fir_node_is_ty(*node_ptr) && !node_set_find(&live_nodes, node_ptr))
            node_vec_push(&dead_nodes, node_ptr);
    }

    VEC_FOREACH(const struct fir_node*, node_ptr, dead_nodes) {
        [[maybe_unused]] bool was_removed = internal_node_set_remove(&mod->nodes, node_ptr);
        assert(was_removed);
    }

    VEC_FOREACH(const struct fir_node*, node_ptr, dead_nodes) {
        free_node((struct fir_node*)*node_ptr);
    }

    cleanup_nominal_nodes(&mod->funcs, &live_nodes);
    cleanup_nominal_nodes(&mod->globals, &live_nodes);
    cleanup_nominal_nodes(&mod->locals, &live_nodes);

    node_vec_destroy(&dead_nodes);
    node_set_destroy(&live_nodes);
}

void fir_node_set_op(struct fir_node* node, size_t op_index, const struct fir_node* op) {
    assert(op_index < node->op_count);
    if (node->ops[op_index])
        forget_use(node, op_index);
    node->ops[op_index] = op;
    if (op)
        record_use(node, op_index);
}

bool fir_node_is_external(const struct fir_node* node) {
    return node_set_find(&fir_node_mod(node)->external_nodes, &node) != NULL;
}

void fir_node_make_external(struct fir_node* node) {
    assert(!fir_node_is_external(node));
    assert(fir_node_can_be_external(node));
    node_set_insert(&fir_node_mod(node)->external_nodes, (const struct fir_node**)&node);
}

void fir_node_make_internal(struct fir_node* node) {
    assert(fir_node_is_external(node));
    node_set_remove(&fir_node_mod(node)->external_nodes, (const struct fir_node**)&node);
}

struct fir_node* const* fir_mod_funcs(const struct fir_mod* mod) {
    return mod->funcs.elems;
}

struct fir_node* const* fir_mod_globals(const struct fir_mod* mod) {
    return mod->globals.elems;
}

size_t fir_mod_func_count(const struct fir_mod* mod) {
    return mod->funcs.elem_count;
}

size_t fir_mod_global_count(const struct fir_mod* mod) {
    return mod->globals.elem_count;
}

const struct fir_node* fir_mem_ty(struct fir_mod* mod) { return mod->mem_ty; }
const struct fir_node* fir_frame_ty(struct fir_mod* mod) { return mod->frame_ty; }
const struct fir_node* fir_ctrl_ty(struct fir_mod* mod) { return mod->ctrl_ty; }
const struct fir_node* fir_noret_ty(struct fir_mod* mod) { return mod->noret_ty; }
const struct fir_node* fir_ptr_ty(struct fir_mod* mod) { return mod->ptr_ty; }

static inline bool is_valid_ty(const struct fir_node* op) {
    return fir_node_is_ty(op) && op->tag != FIR_NORET_TY;
}

const struct fir_node* fir_array_ty(const struct fir_node* elem_ty, size_t size) {
    assert(is_valid_ty(elem_ty));
    struct fir_mod* mod = fir_node_mod(elem_ty);
    return insert_node(mod, (const struct fir_node*)&(struct { FIR_NODE(1) }) {
        .tag = FIR_ARRAY_TY,
        .mod = mod,
        .data.array_dim = size,
        .op_count = 1,
        .ops = { elem_ty }
    });
}

const struct fir_node* fir_dynarray_ty(const struct fir_node* elem_ty) {
    assert(is_valid_ty(elem_ty));
    struct fir_mod* mod = fir_node_mod(elem_ty);
    return insert_node(mod, (const struct fir_node*)&(struct { FIR_NODE(1) }) {
        .tag = FIR_DYNARRAY_TY,
        .mod = mod,
        .op_count = 1,
        .ops = { elem_ty }
    });
}

const struct fir_node* fir_int_ty(struct fir_mod* mod, size_t bitwidth) {
    return insert_node(mod, &(struct fir_node) {
        .tag = FIR_INT_TY,
        .mod = mod,
        .data.bitwidth = bitwidth
    });
}

const struct fir_node* fir_bool_ty(struct fir_mod* mod) {
    return mod->bool_ty;
}

const struct fir_node* fir_float_ty(struct fir_mod* mod, size_t bitwidth) {
    return insert_node(mod, &(struct fir_node) {
        .tag = FIR_FLOAT_TY,
        .mod = mod,
        .data.bitwidth = bitwidth
    });
}

const struct fir_node* fir_tup_ty(struct fir_mod* mod, const struct fir_node* const* elems, size_t elem_count) {
    if (elem_count == 0)
        return fir_unit_ty(mod);

#ifndef NDEBUG
    for (size_t i = 0; i < elem_count; ++i)
        assert(is_valid_ty(elems[i]));
#endif

    struct small_node small_tup_ty = {};
    struct fir_node* tup_ty = (struct fir_node*)&small_tup_ty;
    if (elem_count > SMALL_NODE_OP_COUNT)
        tup_ty = alloc_node(elem_count);
    tup_ty->tag = FIR_TUP_TY;
    tup_ty->mod = mod;
    tup_ty->op_count = elem_count;
    memcpy(tup_ty->ops, elems, sizeof(struct fir_node*) * elem_count);
    const struct fir_node* result = insert_node(mod, tup_ty);
    if (elem_count > SMALL_NODE_OP_COUNT)
        free(tup_ty);
    return result;
}

const struct fir_node* fir_unit_ty(struct fir_mod* mod) {
    return mod->unit_ty;
}

const struct fir_node* fir_func_ty(
    const struct fir_node* param_ty,
    const struct fir_node* ret_ty)
{
    assert(is_valid_ty(param_ty));
    assert(fir_node_is_ty(ret_ty));
    struct fir_mod* mod = fir_node_mod(param_ty);
    return insert_node(mod, (const struct fir_node*)&(struct { FIR_NODE(2) }) {
        .tag = FIR_FUNC_TY,
        .mod = mod,
        .op_count = 2,
        .ops = { param_ty, ret_ty }
    });
}

const struct fir_node* fir_mem_func_ty(
    const struct fir_node* param_ty,
    const struct fir_node* ret_ty)
{
    const struct fir_node* mem_ty = fir_mem_ty(fir_node_mod(param_ty));
    return fir_func_ty(fir_node_prepend(param_ty, &mem_ty, 1), fir_node_prepend(ret_ty, &mem_ty, 1));
}

const struct fir_node* fir_cont_ty(const struct fir_node* param_ty) {
    return fir_func_ty(param_ty, fir_noret_ty(fir_node_mod(param_ty)));
}

const struct fir_node* fir_mem_cont_ty(const struct fir_node* param_ty) {
    struct fir_mod* mod = fir_node_mod(param_ty);
    const struct fir_node* mem_ty = fir_mem_ty(mod);
    const struct fir_node* noret_ty = fir_noret_ty(mod);
    return fir_func_ty(fir_node_prepend(param_ty, &mem_ty, 1), noret_ty);
}

struct fir_node* fir_func(const struct fir_node* func_ty) {
    assert(func_ty->tag == FIR_FUNC_TY);
    struct fir_mod* mod = fir_node_mod(func_ty);
    struct fir_node* func = alloc_node(1);
    func->id = mod->cur_id++;
    func->tag = FIR_FUNC;
    func->ty = func_ty;
    func->op_count = 1;
    func->props |= FIR_PROP_INVARIANT;
    nominal_node_vec_push(&mod->funcs, &func);
    return func;
}

struct fir_node* fir_cont(const struct fir_node* param_ty) {
    return fir_func(fir_cont_ty(param_ty));
}

static inline bool is_valid_pointee_ty(const struct fir_node* ty) {
    return is_valid_ty(ty) && ty->tag != FIR_MEM_TY;
}

struct fir_node* fir_global(struct fir_mod* mod) {
    struct fir_node* global = alloc_node(1);
    global->id = mod->cur_id++;
    global->tag = FIR_GLOBAL;
    global->ty = fir_ptr_ty(mod);
    global->op_count = 1;
    global->props |= FIR_PROP_INVARIANT;
    nominal_node_vec_push(&mod->globals, &global);
    return global;
}

const struct fir_node* fir_top(const struct fir_node* ty) {
    assert(is_valid_ty(ty));
    return insert_node(fir_node_mod(ty), &(struct fir_node) {
        .tag = FIR_TOP,
        .ty = ty,
    });
}

const struct fir_node* fir_bot(const struct fir_node* ty) {
    assert(is_valid_ty(ty));
    return insert_node(fir_node_mod(ty), &(struct fir_node) {
        .tag = FIR_BOT,
        .ty = ty,
    });
}

const struct fir_node* fir_bool_const(struct fir_mod* mod, bool bool_val) {
    return fir_int_const(fir_bool_ty(mod), bool_val ? 1 : 0);
}

const struct fir_node* fir_int_const(const struct fir_node* ty, fir_int_val int_val) {
    assert(ty->tag == FIR_INT_TY);
    int_val &= make_bitmask(ty->data.bitwidth);
    return insert_node(fir_node_mod(ty), &(struct fir_node) {
        .tag = FIR_CONST,
        .ty = ty,
        .data.int_val = int_val
    });
}

const struct fir_node* fir_float_const(const struct fir_node* ty, fir_float_val float_val) {
    assert(ty->tag == FIR_FLOAT_TY);
    return insert_node(fir_node_mod(ty), &(struct fir_node) {
        .tag = FIR_CONST,
        .ty = ty,
        .data.float_val = float_val
    });
}

const struct fir_node* fir_zero(const struct fir_node* ty) {
    assert(ty->tag == FIR_INT_TY || ty->tag == FIR_FLOAT_TY);
    return ty->tag == FIR_INT_TY ? fir_int_const(ty, 0) : fir_float_const(ty, 0.);
}

const struct fir_node* fir_one(const struct fir_node* ty) {
    assert(ty->tag == FIR_INT_TY || ty->tag == FIR_FLOAT_TY);
    return ty->tag == FIR_INT_TY ? fir_int_const(ty, 1) : fir_float_const(ty, 1.);
}

const struct fir_node* fir_all_ones(const struct fir_node* ty) {
    assert(ty->tag == FIR_INT_TY);
    return fir_int_const(ty, make_bitmask(ty->data.bitwidth));
}

static inline bool is_commutative(enum fir_node_tag tag) {
    switch (tag) {
        case FIR_AND:
        case FIR_OR:
        case FIR_XOR:
        case FIR_IADD:
        case FIR_IMUL:
        case FIR_FADD:
        case FIR_FMUL:
            return true;
        default:
            return false;
    }
}

static inline bool should_swap_ops(
    enum fir_node_tag tag,
    [[maybe_unused]] const struct fir_node* left,
    const struct fir_node* right)
{
    assert(left->tag != FIR_CONST || right->tag != FIR_CONST); // Must be run after constant folding
    return right->tag == FIR_CONST && is_commutative(tag);
}

static inline fir_int_val eval_iarith_op(
    enum fir_node_tag tag,
    size_t bitwidth,
    fir_int_val left_val,
    fir_int_val right_val)
{
    if (tag == FIR_IADD) return left_val + right_val;
    if (tag == FIR_ISUB) return left_val - right_val;
    if (tag == FIR_IMUL) return left_val * right_val;
    if (tag == FIR_UDIV) return left_val / right_val;
    if (tag == FIR_UREM) return left_val % right_val;
    if (tag == FIR_SDIV) {
        return
            ((signed_int_val)sign_extend(left_val, bitwidth)) /
            ((signed_int_val)sign_extend(right_val, bitwidth));
    }
    if (tag == FIR_SREM) {
        return
            ((signed_int_val)sign_extend(left_val, bitwidth)) %
            ((signed_int_val)sign_extend(right_val, bitwidth));
    }
    assert(false && "invalid integer arithmetic operation");
    return 0;
}

const struct fir_node* fir_iarith_op(
    enum fir_node_tag tag,
    const struct fir_node* ctrl,
    const struct fir_node* left,
    const struct fir_node* right)
{
    assert(left->ty == right->ty);
    assert(left->ty->tag == FIR_INT_TY);
    assert(fir_node_tag_is_iarith_op(tag));

    // iadd(const[i], const[j]) -> const[i + j]
    // isub(const[i], const[j]) -> const[i - j]
    // imul(const[i], const[j]) -> const[i * j]
    if (left->tag == FIR_CONST && right->tag == FIR_CONST) {
        return fir_int_const(left->ty, eval_iarith_op(tag,
            left->ty->data.bitwidth,
            left->data.int_val,
            right->data.int_val));
    }

    // iarith_op(x, const[i]) -> iarith_op(const[i], x)
    if (should_swap_ops(tag, left, right))
        return fir_iarith_op(tag, ctrl, right, left);

    // isub(x, x) -> const[0]
    if (left == right && tag == FIR_ISUB)
        return fir_zero(left->ty);

    // isub(x, const[0]) -> x
    if (fir_node_is_zero(right) && tag == FIR_ISUB)
        return left;

    const bool is_div_or_rem = tag == FIR_SDIV || tag == FIR_SREM || tag == FIR_UDIV || tag == FIR_UREM;

    // iadd(const[0], x) -> x
    // imul(const[0], x) -> const[0]
    // sdiv(const[0], x) -> const[0]
    // udiv(const[0], x) -> const[0]
    // srem(const[0], x) -> const[0]
    // urem(const[0], x) -> const[0]
    if (fir_node_is_zero(left)) {
        if (tag == FIR_IADD) return right;
        if (tag == FIR_IMUL || is_div_or_rem) return left;
    }

    // imul(const[1], x) -> x
    if (fir_node_is_one(left) && tag == FIR_IMUL)
        return right;

    // sdiv(x, const[1]) -> x
    // udiv(x, const[1]) -> x
    // srem(x, const[1]) -> x
    // urem(x, const[1]) -> x
    if (fir_node_is_one(right) && is_div_or_rem)
        return left;

    return insert_node(fir_node_mod(left), (const struct fir_node*)&(struct { FIR_NODE(2) }) {
        .tag = tag,
        .op_count = 2,
        .ty = left->ty,
        .ctrl = ctrl,
        .ops = { left, right }
    });
}

static inline double eval_farith_op(
    enum fir_node_tag tag,
    size_t bitwidth,
    double left_val,
    double right_val)
{
    if (bitwidth == 64) {
        if (tag == FIR_FADD) return ((double)left_val) + ((double)right_val);
        if (tag == FIR_FSUB) return ((double)left_val) - ((double)right_val);
        if (tag == FIR_FMUL) return ((double)left_val) * ((double)right_val);
        if (tag == FIR_FDIV) return ((double)left_val) / ((double)right_val);
        if (tag == FIR_FREM) return fmod((double)left_val, (double)right_val);
    } else if (bitwidth == 32) {
        if (tag == FIR_FADD) return ((float)left_val) + ((float)right_val);
        if (tag == FIR_FSUB) return ((float)left_val) - ((float)right_val);
        if (tag == FIR_FMUL) return ((float)left_val) * ((float)right_val);
        if (tag == FIR_FDIV) return ((float)left_val) / ((float)right_val);
        if (tag == FIR_FREM) return fmodf((float)left_val, (float)right_val);
    }
    assert(false && "invalid floating-point arithmetic operation");
    return 0;
}

const struct fir_node* fir_farith_op(
    enum fir_node_tag tag,
    enum fir_fp_flags fp_flags,
    const struct fir_node* ctrl,
    const struct fir_node* left,
    const struct fir_node* right)
{
    assert(left->ty == right->ty);
    assert(left->ty->tag == FIR_FLOAT_TY);
    assert(fir_node_tag_is_farith_op(tag));

    // fadd(const[i], const[j]) -> const[i + j]
    // fsub(const[i], const[j]) -> const[i - j]
    // fmul(const[i], const[j]) -> const[i * j]
    if (left->tag == FIR_CONST && right->tag == FIR_CONST) {
        return fir_float_const(left->ty, eval_farith_op(tag,
            left->ty->data.bitwidth,
            left->data.float_val,
            right->data.float_val));
    }

    // farith_op(x, const[i]) -> farith_op(const[i], x)
    if (should_swap_ops(tag, left, right))
        return fir_farith_op(tag, fp_flags, ctrl, right, left);

    const bool is_finite_only = (fp_flags & FIR_FP_FINITE_ONLY) != 0;

    // fsub(x, x) -> const[0]
    if (left == right && tag == FIR_FSUB && is_finite_only)
        return fir_zero(left->ty);

    // fsub(x, const[0]) -> x
    if (fir_node_is_zero(right) && tag == FIR_FSUB)
        return left;

    // fadd(const[0], x) -> x
    // fmul(const[0], x) -> const[0]
    // fdiv(const[0], x) -> const[0]
    // frem(const[0], x) -> const[0]
    if (fir_node_is_zero(left)) {
        if (tag == FIR_FADD) return right;
        if ((tag == FIR_FMUL || tag == FIR_FDIV || tag == FIR_FREM) && is_finite_only) return left;
    }

    // fmul(const[1], x) -> x
    if (fir_node_is_one(left) && tag == FIR_FMUL)
        return right;

    // fdiv(x, const[1]) -> x
    // frem(x, const[1]) -> x
    if (fir_node_is_one(right) && (tag == FIR_FDIV || tag == FIR_FREM))
        return left;

    return insert_node(fir_node_mod(left), (const struct fir_node*)&(struct { FIR_NODE(2) }) {
        .tag = tag,
        .data.fp_flags = fp_flags,
        .op_count = 2,
        .ty = left->ty,
        .ctrl = ctrl,
        .ops = { left, right }
    });
}

const struct fir_node* fir_icmp_op(
    enum fir_node_tag tag,
    const struct fir_node* ctrl,
    const struct fir_node* left,
    const struct fir_node* right)
{
    assert(left->ty == right->ty);
    assert(left->ty->tag == FIR_INT_TY);
    assert(fir_node_tag_is_icmp_op(tag));
    struct fir_mod* mod = fir_node_mod(left);
    return insert_node(mod, (const struct fir_node*)&(struct { FIR_NODE(2) }) {
        .tag = tag,
        .op_count = 2,
        .ty = fir_bool_ty(mod),
        .ctrl = ctrl,
        .ops = { left, right }
    });
}

const struct fir_node* fir_fcmp_op(
    enum fir_node_tag tag,
    const struct fir_node* ctrl,
    const struct fir_node* left,
    const struct fir_node* right)
{
    assert(left->ty == right->ty);
    assert(left->ty->tag == FIR_FLOAT_TY);
    assert(fir_node_tag_is_fcmp_op(tag));
    struct fir_mod* mod = fir_node_mod(left);
    return insert_node(mod, (const struct fir_node*)&(struct { FIR_NODE(2) }) {
        .tag = tag,
        .op_count = 2,
        .ty = fir_bool_ty(mod),
        .ctrl = ctrl,
        .ops = { left, right }
    });
}

static inline fir_int_val eval_bit_op(enum fir_node_tag tag, fir_int_val left_val, fir_int_val right_val) {
    if (tag == FIR_AND) return left_val & right_val;
    if (tag == FIR_OR)  return left_val | right_val;
    if (tag == FIR_XOR) return left_val ^ right_val;
    assert(false && "invalid bitwise operation");
    return 0;
}

const struct fir_node* fir_bit_op(
    enum fir_node_tag tag,
    const struct fir_node* ctrl,
    const struct fir_node* left,
    const struct fir_node* right)
{
    assert(left->ty == right->ty);
    assert(left->ty->tag == FIR_INT_TY);
    assert(fir_node_tag_is_bit_op(tag));

    // and(const[i], const[j]) -> const[i & j]
    // or (const[i], const[j]) -> const[i | j]
    // xor(const[i], const[j]) -> const[i ^ j]
    if (left->tag == FIR_CONST && right->tag == FIR_CONST)
        return fir_int_const(left->ty, eval_bit_op(tag, left->data.int_val, right->data.int_val));

    // farith_op(x, const[i]) -> farith_op(const[i], x)
    if (should_swap_ops(tag, left, right))
        return fir_bit_op(tag, ctrl, right, left);

    // and(x, x) -> x
    // or(x, x) -> x
    // xor(x, x) -> const[0]
    if (left == right) {
        if (tag == FIR_AND || tag == FIR_OR) return left;
        if (tag == FIR_XOR) return fir_zero(left->ty);
    }

    // and(const[0], x) -> const[0]
    // or(const[0], x) -> x
    // xor(const[0], x) -> x
    if (fir_node_is_zero(left)) {
        if (tag == FIR_AND) return left;
        if (tag == FIR_OR || tag == FIR_XOR) return right;
    }

    // and(const[-1], x) -> x
    // or(const[-1], x) -> const[-1]
    if (fir_node_is_all_ones(left)) {
        if (tag == FIR_AND) return right;
        if (tag == FIR_OR) return left;
    }

    return insert_node(fir_node_mod(left), (const struct fir_node*)&(struct { FIR_NODE(2) }) {
        .tag = tag,
        .op_count = 2,
        .ty = left->ty,
        .ctrl = ctrl,
        .ops = { left, right }
    });
}

static inline fir_int_val eval_shift_op(
    enum fir_node_tag tag,
    size_t bitwidth,
    fir_int_val left,
    fir_int_val right)
{
    if (tag == FIR_SHL)  return left << right;
    if (tag == FIR_LSHR) return left >> right;
    if (tag == FIR_ASHR) return ((signed_int_val)sign_extend(left, bitwidth)) >> right;
    assert("invalid shift operation");
    return 0;
}

const struct fir_node* fir_shift_op(
    enum fir_node_tag tag,
    const struct fir_node* ctrl,
    const struct fir_node* val,
    const struct fir_node* amount)
{
    assert(fir_node_tag_is_shift_op(tag));
    assert(amount->ty->tag == FIR_INT_TY);
    assert(val->ty->tag == FIR_INT_TY);

    // shl (const[i], const[j]) -> const[i << j]
    // ashr(const[i], const[j]) -> const[i >>(arith) j]
    // lshr(const[i], const[j]) -> const[i >>(logic) j]
    if (val->tag == FIR_CONST && val->tag == FIR_CONST) {
        fir_int_val int_val = eval_shift_op(tag,
            val->ty->data.bitwidth,
            val->data.int_val,
            amount->data.int_val);
        return fir_int_const(val->ty, int_val);
    }

    // shl(x, const[0]) -> x
    // ashr(x, const[0]) -> x
    // lshr(x, const[0]) -> x
    // shl(const[0], x) -> const[0]
    // ashr(const[0], x) -> const[0]
    // lshr(const[0], x) -> const[0]
    if (fir_node_is_zero(amount) || fir_node_is_zero(val))
        return val;

    return insert_node(fir_node_mod(val), (const struct fir_node*)&(struct { FIR_NODE(2) }) {
        .tag = tag,
        .op_count = 2,
        .ty = val->ty,
        .ctrl = ctrl,
        .ops = { val, amount }
    });
}

static inline bool is_cast_possible(
    enum fir_node_tag tag,
    const struct fir_node* ty,
    const struct fir_node* arg_ty)
{
    assert(ty->tag == FIR_INT_TY || ty->tag == FIR_FLOAT_TY);
    assert(arg_ty->tag == FIR_INT_TY || arg_ty->tag == FIR_FLOAT_TY);
    switch (tag) {
        case FIR_BITCAST:
            return arg_ty->data.bitwidth == ty->data.bitwidth;
        case FIR_UTOF:
        case FIR_STOF:
            return arg_ty->tag == FIR_INT_TY && ty->tag == FIR_FLOAT_TY;
        case FIR_FTOU:
        case FIR_FTOS:
            return arg_ty->tag == FIR_FLOAT_TY && ty->tag == FIR_INT_TY;
        case FIR_ZEXT:
        case FIR_SEXT:
            return arg_ty->tag == FIR_INT_TY && ty->tag == FIR_INT_TY && arg_ty->data.bitwidth <= ty->data.bitwidth;
        case FIR_ITRUNC:
            return arg_ty->tag == FIR_INT_TY && ty->tag == FIR_INT_TY && arg_ty->data.bitwidth >= ty->data.bitwidth;
        case FIR_FTRUNC:
            return arg_ty->tag == FIR_FLOAT_TY && ty->tag == FIR_FLOAT_TY && arg_ty->data.bitwidth >= ty->data.bitwidth;
        default:
            assert(false && "invalid cast tag");
            return false;
    }
}

static inline const struct fir_node* eval_bitcast(
    const struct fir_node* ty,
    const struct fir_node* arg)
{
    assert(arg->tag == FIR_CONST);
    if (arg->ty->tag == FIR_INT_TY && ty->tag == FIR_FLOAT_TY) {
        if (ty->data.bitwidth == 32)
            return fir_float_const(ty, bits_to_float(arg->data.int_val));
        if (ty->data.bitwidth == 64)
            return fir_float_const(ty, bits_to_double(arg->data.int_val));
    } else if (arg->ty->tag == FIR_FLOAT_TY && ty->tag == FIR_INT_TY) {
        if (ty->data.bitwidth == 32)
            return fir_int_const(ty, float_to_bits(arg->data.float_val));
        if (ty->data.bitwidth == 64)
            return fir_int_const(ty, double_to_bits(arg->data.float_val));
    }
    assert(false && "invalid bitcast");
    return NULL;
}

static inline fir_float_val eval_ftrunc(size_t bitwidth, fir_float_val arg_val) {
    if (bitwidth == 32)
        return (float)arg_val;
    assert(false && "invalid floating-point truncation");
    return 0.;
}

static inline fir_float_val eval_utof(size_t bitwidth, fir_int_val arg_val) {
    if (bitwidth == 32)
        return (float)arg_val;
    if (bitwidth == 64)
        return (double)arg_val;
    assert(false && "invalid unsigned integer to floating-point number cast");
    return 0.;
}

static inline fir_float_val eval_stof(size_t bitwidth, fir_int_val arg_val) {
    intmax_t signed_val = arg_val;
    if (bitwidth == 32)
        return (float)signed_val;
    if (bitwidth == 64)
        return (double)signed_val;
    assert(false && "invalid signed integer to floating-point number cast");
    return 0.;
}

static inline fir_int_val eval_ftou(size_t bitwidth, fir_float_val arg_val) {
    if (bitwidth == 32)
        return (fir_int_val)(float)arg_val;
    if (bitwidth == 64)
        return (fir_int_val)(double)arg_val;
    assert(false && "invalid floating-point number to unsigned integer cast");
    return 0;
}

static inline fir_int_val eval_ftos(size_t bitwidth, fir_float_val arg_val) {
    if (bitwidth == 32)
        return (fir_int_val)(intmax_t)(float)arg_val;
    if (bitwidth == 64)
        return (fir_int_val)(intmax_t)(double)arg_val;
    assert(false && "invalid floating-point number to signed integer cast");
    return 0;
}

const struct fir_node* fir_cast_op(
    enum fir_node_tag tag,
    const struct fir_node* ctrl,
    const struct fir_node* ty,
    const struct fir_node* arg)
{
    assert(fir_node_tag_is_cast_op(tag));
    assert(is_cast_possible(tag, ty, arg->ty));

    if (arg->ty == ty)
        return arg;

    if (arg->tag == FIR_CONST) {
        if (tag == FIR_BITCAST)
            return eval_bitcast(ty, arg);
        if (tag == FIR_FTRUNC)
            return fir_float_const(ty, eval_ftrunc(ty->data.bitwidth, arg->data.float_val));
        if (tag == FIR_ZEXT || tag == FIR_ITRUNC)
            return fir_int_const(ty, arg->data.int_val);
        if (tag == FIR_SEXT)
            return fir_int_const(ty, sign_extend(arg->data.int_val, arg->ty->data.bitwidth));
        if (tag == FIR_UTOF)
            return fir_float_const(ty, eval_utof(ty->data.bitwidth, arg->data.int_val));
        if (tag == FIR_STOF)
            return fir_float_const(ty, eval_stof(ty->data.bitwidth, arg->data.int_val));
        if (tag == FIR_FTOU)
            return fir_int_const(ty, eval_ftou(arg->ty->data.bitwidth, arg->data.float_val));
        if (tag == FIR_FTOS)
            return fir_int_const(ty, eval_ftos(arg->ty->data.bitwidth, arg->data.float_val));
        assert(false && "invalid cast operation");
        return NULL;
    }

    return insert_node(fir_node_mod(ty), (const struct fir_node*)&(struct { FIR_NODE(1) }) {
        .tag = tag,
        .op_count = 1,
        .ty = ty,
        .ctrl = ctrl,
        .ops = { arg }
    });
}

const struct fir_node* fir_not(const struct fir_node* ctrl, const struct fir_node* arg) {
    return fir_bit_op(FIR_XOR, ctrl, fir_int_const(arg->ty, make_bitmask(arg->ty->data.bitwidth)), arg);
}

const struct fir_node* fir_ineg(const struct fir_node* ctrl, const struct fir_node* arg) {
    return fir_iarith_op(FIR_ISUB, ctrl, fir_int_const(arg->ty, 0), arg);
}

const struct fir_node* fir_fneg(
    enum fir_fp_flags fp_flags,
    const struct fir_node* ctrl,
    const struct fir_node* arg)
{
    return fir_farith_op(FIR_FSUB, fp_flags, ctrl, fir_float_const(arg->ty, 0), arg);
}

static const struct fir_node* infer_tup_ty(
    struct fir_mod* mod,
    const struct fir_node* const* elems,
    size_t elem_count)
{
    struct small_node_vec small_ops;
    small_node_vec_init(&small_ops);
    for (size_t i = 0; i < elem_count; ++i)
        small_node_vec_push(&small_ops, &elems[i]->ty);
    const struct fir_node* tup_ty = fir_tup_ty(mod, small_ops.elems, elem_count);
    small_node_vec_destroy(&small_ops);
    return tup_ty;
}

static inline bool is_from_exts(
    const struct fir_node* aggr_ty,
    const struct fir_node*const* elems,
    size_t elem_count)
{
    assert(aggr_ty->tag == FIR_TUP_TY || aggr_ty->tag == FIR_ARRAY_TY);
    if (elem_count == 0 || elems[0]->tag != FIR_EXT || FIR_EXT_AGGR(elems[0])->ty != aggr_ty)
        return false;
    for (size_t i = 0; i < elem_count; ++i) {
        if (elems[i]->tag != FIR_EXT ||
            FIR_EXT_AGGR(elems[i]) != FIR_EXT_AGGR(elems[0]) ||
            !fir_node_is_int_const(FIR_EXT_INDEX(elems[i])) ||
            FIR_EXT_INDEX(elems[i])->data.int_val != i)
            return false;
    }
    return true;
}

const struct fir_node* fir_tup(
    struct fir_mod* mod,
    const struct fir_node* ctrl,
    const struct fir_node* const* elems,
    size_t elem_count)
{
    if (elem_count == 0)
        return fir_unit(mod);

    // tup(ext(x, 0), ext(x, 1), ..., ext(x, n)) -> x
    const struct fir_node* tup_ty = infer_tup_ty(mod, elems, elem_count);
    if (is_from_exts(tup_ty, elems, elem_count))
        return FIR_EXT_AGGR(elems[0]);

    struct small_node small_tup = {};
    struct fir_node* tup = (struct fir_node*)&small_tup;
    if (elem_count > SMALL_NODE_OP_COUNT)
        tup = alloc_node(elem_count);
    tup->tag = FIR_TUP;
    tup->ty = tup_ty;
    tup->op_count = elem_count;
    tup->ctrl = ctrl;
    memcpy(tup->ops, elems, sizeof(struct fir_node*) * elem_count);
    const struct fir_node* result = insert_node(mod, tup);
    if (elem_count > SMALL_NODE_OP_COUNT)
        free(tup);
    return result;
}

const struct fir_node* fir_unit(struct fir_mod* mod) {
    return mod->unit;
}

const struct fir_node* fir_array(
    const struct fir_node* ctrl,
    const struct fir_node* ty,
    const struct fir_node* const* elems)
{
    // array(ext(x, 0), ext(x, 1), ..., ext(x, n)) -> x
    if (is_from_exts(ty, elems, ty->data.array_dim))
        return FIR_EXT_AGGR(elems[0]);

#ifndef NDEBUG
    for (size_t i = 1; i < ty->data.array_dim; ++i)
        assert(elems[i]->ty == elems[0]->ty);
#endif

    assert(ty->tag == FIR_ARRAY_TY);
    struct fir_mod* mod = fir_node_mod(ty);
    struct small_node small_array = {};
    struct fir_node* array = (struct fir_node*)&small_array;
    if (ty->data.array_dim > SMALL_NODE_OP_COUNT)
        array = alloc_node(ty->data.array_dim);
    array->tag = FIR_ARRAY;
    array->ty = ty;
    array->ctrl = ctrl;
    array->op_count = ty->data.array_dim;
    memcpy(array->ops, elems, sizeof(struct fir_node*) * ty->data.array_dim);
    const struct fir_node* result = insert_node(mod, array);
    if (ty->data.array_dim > SMALL_NODE_OP_COUNT)
        free(array);
    return result;
}

static const struct fir_node* infer_ext_ty(
    const struct fir_node* aggr_ty,
    const struct fir_node* index)
{
    assert(index->ty->tag == FIR_INT_TY);
    if (aggr_ty->tag == FIR_TUP_TY) {
        assert(index->tag == FIR_CONST);
        return aggr_ty->ops[index->data.int_val];
    } else {
        assert(aggr_ty->tag == FIR_ARRAY_TY);
        return aggr_ty->ops[0];
    }
}

static inline bool same_ops(const struct fir_node* const* ops, size_t op_count) {
    assert(op_count != 0);
    for (size_t i = 1; i < op_count; ++i) {
        if (ops[0] != ops[i])
            return false;
    }
    return true;
}

static inline const struct fir_node* find_ins(
    const struct fir_node* aggr,
    const struct fir_node* index)
{
    while (true) {
        if (aggr->tag != FIR_INS)
            break;
        if (FIR_INS_INDEX(aggr) == index)
            return aggr;
        // The insertion must use a constant index, otherwise it is not guaranteed to insert the
        // element at the same position in the aggregate.
        if (index->tag != FIR_CONST || FIR_INS_INDEX(aggr)->tag != FIR_CONST)
            break;
        aggr = FIR_INS_AGGR(aggr);
    }
    return NULL;
}

const struct fir_node* fir_ext(
    const struct fir_node* ctrl,
    const struct fir_node* aggr,
    const struct fir_node* index)
{
    // ext(tup(x1, ..., xn), const[i]) -> xi
    // ext(array(x1, ..., xn), const[i]) -> xi
    if (aggr->tag == FIR_TUP || (aggr->tag == FIR_ARRAY && index->tag == FIR_CONST)) {
        assert(fir_node_is_int_const(index));
        assert(index->data.int_val < aggr->op_count);
        return aggr->ops[index->data.int_val];
    }

    // ext(array(x, x, x, x, ...), y) -> x
    if (aggr->tag == FIR_ARRAY && aggr->op_count > 0 && same_ops(aggr->ops, aggr->op_count))
        return aggr->ops[0];

    // ext(array(x, y), not x) -> ext(array(y, x), x)
    if (aggr->tag == FIR_ARRAY &&
        aggr->op_count == 2 &&
        fir_node_is_not(index) &&
        fir_node_is_bool_ty(index))
    {
        const struct fir_node* swapped_ops[] = { aggr->ops[1], aggr->ops[0] };
        return fir_ext(ctrl, fir_array(ctrl, aggr->ty, swapped_ops), index->ops[1]);
    }

    // ext(ins(ins(...(ins(x, const[i], y)...))), const[i]) -> y
    const struct fir_node* ins = find_ins(aggr, index);
    if (ins)
        return FIR_INS_ELEM(ins);

    return insert_node(fir_node_mod(aggr), (const struct fir_node*)&(struct { FIR_NODE(2) }) {
        .tag = FIR_EXT,
        .op_count = 2,
        .ty = infer_ext_ty(aggr->ty, index),
        .ctrl = ctrl,
        .ops = { aggr, index }
    });
}

static inline const struct fir_node* remove_ins(
    const struct fir_node* ctrl,
    const struct fir_node* aggr,
    const struct fir_node* ins)
{
    struct small_node_vec stack;
    small_node_vec_init(&stack);
    while (true) {
        assert(aggr->tag == FIR_INS);
        if (aggr == ins)
            break;
        small_node_vec_push(&stack, &FIR_INS_AGGR(aggr));
        aggr = FIR_INS_AGGR(aggr);
    }
    const struct fir_node* result = FIR_INS_AGGR(ins);
    for (size_t i = stack.elem_count; i-- > 0;)
        result = fir_ins(ctrl, result, FIR_INS_INDEX(stack.elems[i]), FIR_INS_ELEM(stack.elems[i]));
    return result;
}

const struct fir_node* fir_ext_at(
    const struct fir_node* ctrl,
    const struct fir_node* aggr,
    size_t index)
{
    return fir_ext(ctrl, aggr, fir_int_const(fir_node_mod(aggr)->index_ty, index));
}

const struct fir_node* fir_ext_mem(const struct fir_node* ctrl, const struct fir_node* val) {
    if (val->ty->tag == FIR_MEM_TY)
        return val;
    if (val->ty->tag == FIR_TUP_TY) {
        for (size_t i = 0; i < val->ty->op_count; ++i) {
            const struct fir_node* mem = fir_ext_mem(ctrl, fir_ext_at(ctrl, val, i));
            if (mem)
                return mem;
        }
    }
    return NULL;
}

const struct fir_node* fir_ins(
    const struct fir_node* ctrl,
    const struct fir_node* aggr,
    const struct fir_node* index,
    const struct fir_node* elem)
{
    assert(infer_ext_ty(aggr->ty, index) == elem->ty);

    // ins(tup(x1, ..., xn), const[i], y) -> tup(x1, ...,, xi-1, y, xi+1, ..., xn)
    // ins(array(x1, ..., xn), const[i], y) -> array(x1, ...,, xi-1, y, xi+1, ..., xn)
    if ((aggr->tag == FIR_TUP || aggr->tag == FIR_ARRAY) && index->tag == FIR_CONST) {
        struct small_node_vec small_ops;
        small_node_vec_init(&small_ops);
        for (size_t i = 0; i < aggr->op_count; ++i)
            small_node_vec_push(&small_ops, i == index->data.int_val ? &elem : &aggr->ops[i]);
        const struct fir_node* ins_aggr = aggr->tag == FIR_TUP
            ? fir_tup(fir_node_mod(aggr), ctrl, small_ops.elems, small_ops.elem_count)
            : fir_array(ctrl, aggr->ty, small_ops.elems);
        small_node_vec_destroy(&small_ops);
        return ins_aggr;
    }

    // ins(ins(...(ins(x, const[i], y)...)), const[i], z) -> ins(ins(...x...), const[i], z)
    const struct fir_node* ins = find_ins(aggr, index);
    if (ins)
        aggr = remove_ins(ctrl, aggr, ins);

    return insert_node(fir_node_mod(aggr), (const struct fir_node*)&(struct { FIR_NODE(3) }) {
        .tag = FIR_INS,
        .op_count = 3,
        .ty = aggr->ty,
        .ctrl = ctrl,
        .ops = { aggr, index, elem }
    });
}

const struct fir_node* fir_ins_at(
    const struct fir_node* ctrl,
    const struct fir_node* aggr,
    size_t index,
    const struct fir_node* elem)
{
    return fir_ins(ctrl, aggr, fir_int_const(fir_node_mod(aggr)->index_ty, index), elem);
}

const struct fir_node* fir_ins_mem(
    const struct fir_node* ctrl,
    const struct fir_node* val,
    const struct fir_node* mem)
{
    assert(mem->ty->tag == FIR_MEM_TY);
    if (val->ty->tag == FIR_MEM_TY)
        return mem;
    if (val->ty->tag == FIR_TUP_TY) {
        for (size_t i = 0; i < val->ty->op_count; ++i) {
            const struct fir_node* elem = fir_ext_at(ctrl, val, i);
            const struct fir_node* elem_with_mem = fir_ins_mem(ctrl, elem, mem);
            if (elem_with_mem != elem)
                return fir_ins_at(ctrl, val, i, elem_with_mem);
        }
    }
    return val;
}

const struct fir_node* fir_addrof(
    const struct fir_node* ctrl,
    const struct fir_node* ptr,
    const struct fir_node* aggr_ty,
    const struct fir_node* index)
{
    assert(ptr->ty->tag == FIR_PTR_TY);
    assert(infer_ext_ty(aggr_ty, index));
    return insert_node(fir_node_mod(ptr), (const struct fir_node*)&(struct { FIR_NODE(3) }) {
        .tag = FIR_ADDROF,
        .op_count = 3,
        .ty = ptr->ty,
        .ctrl = ctrl,
        .ops = { ptr, aggr_ty, index }
    });
}

const struct fir_node* fir_addrof_at(
    const struct fir_node* ctrl,
    const struct fir_node* ptr,
    const struct fir_node* aggr_ty,
    size_t index)
{
    return fir_addrof(ctrl, ptr, aggr_ty, fir_int_const(fir_node_mod(aggr_ty)->index_ty, index));
}

const struct fir_node* fir_select(
    const struct fir_node* ctrl,
    const struct fir_node* cond,
    const struct fir_node* when_true,
    const struct fir_node* when_false)
{
    return fir_choice(ctrl, cond, (const struct fir_node*[]) { when_false, when_true }, 2);
}

const struct fir_node* fir_choice(
    const struct fir_node* ctrl,
    const struct fir_node* index,
    const struct fir_node* const* elems,
    size_t elem_count)
{
    assert(elem_count > 0);
    const struct fir_node* array_ty = fir_array_ty(elems[0]->ty, elem_count);
    const struct fir_node* array = fir_array(ctrl, array_ty, elems);
    return fir_ext(ctrl, array, index);
}

struct fir_node* fir_local(
    const struct fir_node* frame,
    const struct fir_node* init)
{
    assert(frame->ty->tag == FIR_FRAME_TY);
    struct fir_mod* mod = fir_node_mod(frame);
    struct fir_node* alloc = alloc_node(2);
    alloc->id = mod->cur_id++;
    alloc->tag = FIR_LOCAL;
    alloc->ty = fir_ptr_ty(mod);
    alloc->op_count = 2;
    fir_node_set_op(alloc, 0, frame);
    fir_node_set_op(alloc, 1, init);
    nominal_node_vec_push(&mod->locals, &alloc);
    return alloc;
}

static inline bool is_from_local(const struct fir_node* ptr) {
    while (true) {
        if (ptr->tag == FIR_LOCAL)
            return true;
        if (ptr->tag != FIR_ADDROF)
            return false;
        ptr = FIR_ADDROF_PTR(ptr);
    }
}

const struct fir_node* fir_load(
    enum fir_mem_flags flags,
    const struct fir_node* ctrl,
    const struct fir_node* mem,
    const struct fir_node* ptr,
    const struct fir_node* ty)
{
    assert(mem->ty->tag == FIR_MEM_TY);
    assert(ptr->ty->tag == FIR_PTR_TY);
    assert(is_valid_pointee_ty(ty));
    struct fir_mod* mod = fir_node_mod(mem);

    // load(store(mem, ptr, val), ptr) -> val
    if (mem->tag == FIR_STORE &&
        FIR_STORE_PTR(mem) == ptr &&
        FIR_STORE_VAL(mem)->ty == ty &&
        (flags & FIR_MEM_VOLATILE) == 0)
    {
        return fir_tup(mod, ctrl, (const struct fir_node*[]) { mem, FIR_STORE_VAL(mem) }, 2);
    }

    if (is_from_local(ptr))
        flags |= FIR_MEM_NON_NULL;

    return insert_node(mod, (const struct fir_node*)&(struct { FIR_NODE(2) }) {
        .tag = FIR_LOAD,
        .op_count = 2,
        .ty = fir_tup_ty(mod, (const struct fir_node*[]) { fir_mem_ty(mod), ty }, 2),
        .data.mem_flags = flags,
        .ctrl = ctrl,
        .ops = { mem, ptr }
    });
}

const struct fir_node* fir_store(
    enum fir_mem_flags flags,
    const struct fir_node* ctrl,
    const struct fir_node* mem,
    const struct fir_node* ptr,
    const struct fir_node* val)
{
    assert(mem->ty->tag == FIR_MEM_TY);
    assert(ptr->ty->tag == FIR_PTR_TY);
    assert(is_valid_pointee_ty(val->ty));

    // store(store(mem, ptr, x), ptr, y) -> store(mem, ptr, y)
    if (mem->tag == FIR_STORE && FIR_STORE_PTR(mem) == ptr && (flags & FIR_MEM_VOLATILE) == 0)
        mem = FIR_STORE_MEM(mem);

    if (val->tag == FIR_BOT)
        return mem;

    if (is_from_local(ptr))
        flags |= FIR_MEM_NON_NULL;

    return insert_node(fir_node_mod(mem), (const struct fir_node*)&(struct { FIR_NODE(3) }) {
        .tag = FIR_STORE,
        .op_count = 3,
        .ty = mem->ty,
        .data.mem_flags = flags,
        .ctrl = ctrl,
        .ops = { mem, ptr, val }
    });
}

const struct fir_node* fir_split(
    const struct fir_node* ctrl,
    const struct fir_node* mem,
    size_t mem_count)
{
    assert(mem->ty->tag == FIR_MEM_TY);

    struct small_node_vec elems;
    small_node_vec_init(&elems);
    for (size_t i = 0; i < mem_count; ++i)
        small_node_vec_push(&elems, &mem->ty);
    struct fir_mod* mod = fir_node_mod(mem);
    const struct fir_node* ty = fir_tup_ty(mod, elems.elems, mem_count);
    small_node_vec_destroy(&elems);

    return insert_node(mod, (const struct fir_node*) &(struct { FIR_NODE(1) }) {
        .tag = FIR_SPLIT,
        .op_count = 1,
        .ty = ty,
        .ctrl = ctrl,
        .ops = { mem }
    });
}

static inline bool is_from_split(const struct fir_node* const* ops, size_t op_count) {
    assert(op_count > 0);
    if (ops[0]->tag != FIR_EXT || FIR_EXT_AGGR(ops[0])->tag != FIR_SPLIT)
        return false;
    const struct fir_node* split = FIR_EXT_AGGR(ops[0]);
    if (op_count != split->ty->op_count)
        return false;
    for (size_t i = 1; i < op_count; ++i) {
        if (ops[i]->tag != FIR_EXT || FIR_EXT_AGGR(ops[i]) != split)
            return false;
    }
    return true;
}

const struct fir_node* fir_join(
    const struct fir_node* ctrl,
    const struct fir_node* const* mem_elems,
    size_t mem_count)
{
    assert(mem_count > 0);
    assert(mem_elems[0]->ty->tag == FIR_MEM_TY);

#ifndef NDEBUG
    for (size_t i = 0; i < mem_count; ++i) {
        for (size_t j = i + 1; j < mem_count; ++j)
            assert(mem_elems[i] != mem_elems[j]);
    }
#endif

    // join(ext(split(m), i), ext(split(m), j), ...) -> m
    if (is_from_split(mem_elems, mem_count))
        return FIR_SPLIT_MEM(FIR_EXT_AGGR(mem_elems[0]));

    struct fir_mod* mod = fir_node_mod(mem_elems[0]);
    struct small_node small_join = {};
    struct fir_node* join = (struct fir_node*)&small_join;
    if (mem_count > SMALL_NODE_OP_COUNT)
        join = alloc_node(mem_count);
    join->tag = FIR_JOIN;
    join->mod = mod;
    join->ctrl = ctrl;
    join->op_count = mem_count;
    memcpy(join->ops, mem_elems, sizeof(struct fir_node*) * mem_count);
    const struct fir_node* result = insert_node(mod, join);
    if (mem_count > SMALL_NODE_OP_COUNT)
        free(join);
    return result;
}

const struct fir_node* fir_call(
    const struct fir_node* ctrl,
    const struct fir_node* callee,
    const struct fir_node* arg)
{
    assert(callee->ty->tag == FIR_FUNC_TY);
    assert(FIR_FUNC_TY_PARAM(callee->ty) == arg->ty);
    return insert_node(fir_node_mod(callee), (const struct fir_node*)&(struct { FIR_NODE(2) }) {
        .tag = FIR_CALL,
        .op_count = 2,
        .ty = FIR_FUNC_TY_RET(callee->ty),
        .ctrl = ctrl,
        .ops = { callee, arg }
    });
}

const struct fir_node* fir_branch(
    const struct fir_node* ctrl,
    const struct fir_node* cond,
    const struct fir_node* arg,
    const struct fir_node* jump_true,
    const struct fir_node* jump_false)
{
    return fir_switch(ctrl, cond, arg, (const struct fir_node*[]) { jump_false, jump_true }, 2);
}

const struct fir_node* fir_switch(
    const struct fir_node* ctrl,
    const struct fir_node* index,
    const struct fir_node* arg,
    const struct fir_node* const* targets,
    size_t target_count)
{
    assert(target_count > 0);
    return fir_call(ctrl, fir_choice(ctrl, index, targets, target_count), arg);
}

const struct fir_node* fir_param(const struct fir_node* func) {
    assert(func->tag == FIR_FUNC);
    return insert_node(fir_node_mod(func), (const struct fir_node*)&(struct { FIR_NODE(1) }) {
        .tag = FIR_PARAM,
        .op_count = 1,
        .ty = FIR_FUNC_TY_PARAM(func->ty),
        .ops = { func }
    });
}

const struct fir_node* fir_ctrl(const struct fir_node* func) {
    assert(func->tag == FIR_FUNC);
    struct fir_mod* mod = fir_node_mod(func);
    return insert_node(mod, (const struct fir_node*)&(struct { FIR_NODE(1) }) {
        .tag = FIR_CTRL,
        .op_count = 1,
        .ty = fir_ctrl_ty(mod),
        .ops = { func }
    });
}

const struct fir_node* fir_start(const struct fir_node* block) {
    assert(block->tag == FIR_FUNC);
    assert(fir_node_is_cont_ty(block->ty));
    assert(FIR_FUNC_TY_PARAM(block->ty)->tag == FIR_TUP_TY);
    assert(FIR_FUNC_TY_PARAM(block->ty)->op_count == 2);
    assert(FIR_FUNC_TY_PARAM(block->ty)->ops[0]->tag == FIR_FRAME_TY);
    assert(fir_node_is_cont_ty(FIR_FUNC_TY_PARAM(block->ty)->ops[1]));
    const struct fir_node* ret_ty = FIR_FUNC_TY_PARAM(FIR_FUNC_TY_PARAM(block->ty)->ops[1]);
    return insert_node(fir_node_mod(block), (const struct fir_node*)&(struct { FIR_NODE(1) }) {
        .tag = FIR_START,
        .op_count = 1,
        .ty = ret_ty,
        .ops = { block }
    });
}
