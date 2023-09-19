#include "fir/module.h"
#include "fir/node.h"

#include "support/map.h"
#include "support/bits.h"
#include "support/vec.h"
#include "support/alloc.h"
#include "support/hash.h"
#include "simplify.h"

#include <stdlib.h>

#define SMALL_OP_COUNT 8

static uint32_t hash_node_data(uint32_t h, const struct fir_node* node) {
    if (fir_node_has_fp_flags(node))
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

static uint32_t hash_node(const struct fir_node* const* node_ptr) {
    const struct fir_node* node = *node_ptr;
    uint32_t h = hash_init();
    h = hash_uint32(h, node->tag);
    if (!fir_node_is_ty(node))
        h = hash_uint64(h, node->ty->id);
    h = hash_node_data(h, node);
    h = hash_uint64(h, node->op_count);
    for (size_t i = 0; i < node->op_count; ++i)
        h = hash_uint64(h, node->ops[i]->id);
    return h;
}

static inline bool cmp_node_data(const struct fir_node* node, const struct fir_node* other) {
    if (fir_node_has_fp_flags(node))
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

static inline bool cmp_node(
    const struct fir_node* const* node_ptr,
    const struct fir_node* const* other_ptr)
{
    const struct fir_node* node  = *node_ptr;
    const struct fir_node* other = *other_ptr;
    if (node->tag != other->tag || node->op_count != other->op_count)
        return false;
    if (!fir_node_is_ty(node) && node->ty != other->ty)
        return false;
    if (!cmp_node_data(node, other))
        return false;
    for (size_t i = 0; i < node->op_count; ++i) {
        if (node->ops[i] != other->ops[i])
            return false;
    }
    return true;
}

DEF_MAP(internal_node_map, const struct fir_node*, const struct fir_node*, hash_node, cmp_node, PRIVATE)
DEF_VEC(func_vec, struct fir_node*, PRIVATE)
DEF_VEC(global_vec, struct fir_node*, PRIVATE)

struct fir_mod {
    char* name;
    uint64_t cur_id;
    struct func_vec funcs;
    struct global_vec globals;
    struct internal_node_map nodes;
    const struct fir_node* mem_ty;
    const struct fir_node* err_ty;
    const struct fir_node* noret_ty;
    const struct fir_node* ptr_ty;
    const struct fir_node* unit_ty;
    const struct fir_node* unit;
    const struct fir_node* bool_ty;
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

static inline const struct fir_node* insert_node(struct fir_mod* mod, const struct fir_node* node) {
    assert(!fir_node_tag_is_nominal(node->tag));
    assert(fir_node_mod(node) == mod);
    assert(node->uses == NULL);
    const struct fir_node* const* found = internal_node_map_find(&mod->nodes, &node);
    if (found)
        return *found;
    struct fir_node* new_node = alloc_node(node->op_count);
    memcpy(new_node, node, sizeof(struct fir_node) + sizeof(struct fir_node*) * node->op_count);
    for (size_t i = 0; i < node->op_count; ++i)
        record_use(new_node, i);
    new_node->id = mod->cur_id++;
    const struct fir_node* simplified_node = simplify_node(mod, new_node);
    assert(simplified_node->ty == new_node->ty);
    internal_node_map_insert(&mod->nodes, (const struct fir_node* const*)&new_node, &simplified_node);
    return new_node;
}

struct fir_mod* fir_mod_create(const char* name) {
    struct fir_mod* mod = xcalloc(1, sizeof(struct fir_mod));
    mod->name = strdup(name);
    mod->cur_id = 0;
    mod->nodes   = internal_node_map_create();
    mod->mem_ty   = insert_node(mod, &(struct fir_node) { .tag = FIR_MEM_TY,   .mod = mod });
    mod->err_ty   = insert_node(mod, &(struct fir_node) { .tag = FIR_ERR_TY,   .mod = mod });
    mod->noret_ty = insert_node(mod, &(struct fir_node) { .tag = FIR_NORET_TY, .mod = mod });
    mod->ptr_ty   = insert_node(mod, &(struct fir_node) { .tag = FIR_PTR_TY,   .mod = mod });
    mod->unit_ty  = insert_node(mod, &(struct fir_node) { .tag = FIR_TUP_TY,   .mod = mod });
    mod->unit     = insert_node(mod, &(struct fir_node) { .tag = FIR_TUP,      .ty = mod->unit_ty });
    mod->bool_ty  = fir_int_ty(mod, 1);
    return mod;
}

void fir_mod_destroy(struct fir_mod* mod) {
    free(mod->name);
    FOREACH_MAP_KEY(struct fir_node*, node_ptr, mod->nodes) {
        free_node((struct fir_node*)*node_ptr);
    }
    FOREACH_VEC(struct fir_node*, func_ptr, mod->funcs) {
        free_node((struct fir_node*)*func_ptr);
    }
    FOREACH_VEC(struct fir_node*, global_ptr, mod->globals) {
        free_node((struct fir_node*)*global_ptr);
    }
    internal_node_map_destroy(&mod->nodes);
    func_vec_destroy(&mod->funcs);
    global_vec_destroy(&mod->globals);
    free_uses(mod->free_uses);
    free(mod);
}

void fir_node_set_op(struct fir_node* node, size_t op_index, const struct fir_node* op) {
    assert(op_index < node->op_count);
    if (node->ops[op_index])
        forget_use(node, op_index);
    node->ops[op_index] = op;
    if (op)
        record_use(node, op_index);
}

const struct fir_node* fir_node_rebuild(
    struct fir_mod* mod,
    const struct fir_node* node,
    const struct fir_node* ty,
    const struct fir_node* const* ops)
{
    assert(!fir_node_is_nominal(node));
    struct small_node { FIR_NODE(SMALL_OP_COUNT) } small_node;
    struct fir_node* copy = (struct fir_node*)&small_node;
    if (node->op_count > SMALL_OP_COUNT)
        copy = xmalloc(sizeof(struct fir_node));
    memcpy(copy, node, sizeof(struct fir_node));
    if (fir_node_is_ty(node))
        copy->mod = mod;
    else
        copy->ty = ty;
    memcpy(copy->ops, ops, sizeof(struct fir_node*) * node->op_count);
    const struct fir_node* rebuilt_node = insert_node(mod, copy);
    if (node->op_count > SMALL_OP_COUNT)
        free(copy);
    return rebuilt_node;
}

struct fir_node** fir_mod_funcs(const struct fir_mod* mod) {
    return mod->funcs.elems;
}

struct fir_node** fir_mod_globals(const struct fir_mod* mod) {
    return mod->globals.elems;
}

size_t fir_mod_func_count(const struct fir_mod* mod) {
    return mod->funcs.elem_count;
}

size_t fir_mod_global_count(const struct fir_mod* mod) {
    return mod->globals.elem_count;
}

const struct fir_node* fir_mem_ty(struct fir_mod* mod) { return mod->mem_ty; }
const struct fir_node* fir_err_ty(struct fir_mod* mod) { return mod->err_ty; }
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

const struct fir_node* fir_int_ty(struct fir_mod* mod, uint32_t bitwidth) {
    return insert_node(mod, &(struct fir_node) {
        .tag = FIR_INT_TY,
        .mod = mod,
        .data.bitwidth = bitwidth
    });
}

const struct fir_node* fir_bool_ty(struct fir_mod* mod) {
    return mod->bool_ty;
}

const struct fir_node* fir_float_ty(struct fir_mod* mod, uint32_t bitwidth) {
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

    struct small_tup_ty { FIR_NODE(SMALL_OP_COUNT) } small_tup_ty = {};
    struct fir_node* tup_ty = (struct fir_node*)&small_tup_ty;
    if (elem_count > SMALL_OP_COUNT)
        tup_ty = alloc_node(elem_count);
    tup_ty->tag = FIR_TUP_TY;
    tup_ty->mod = mod;
    tup_ty->op_count = elem_count;
    memcpy(tup_ty->ops, elems, sizeof(struct fir_node*) * elem_count);
    const struct fir_node* result = insert_node(mod, tup_ty);
    if (elem_count > SMALL_OP_COUNT)
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

const struct fir_node* fir_cont_ty(const struct fir_node* param_ty) {
    return fir_func_ty(param_ty, fir_noret_ty(fir_node_mod(param_ty)));
}

struct fir_node* fir_func(const struct fir_node* func_ty) {
    assert(func_ty->tag == FIR_FUNC_TY);
    struct fir_mod* mod = fir_node_mod(func_ty);
    struct fir_node* func = alloc_node(1);
    func->id = mod->cur_id++;
    func->tag = FIR_FUNC;
    func->ty = func_ty;
    func->op_count = 1;
    func->ops[0] = NULL;
    func_vec_push(&mod->funcs, &func);
    return func;
}

struct fir_node* fir_cont(const struct fir_node* param_ty) {
    return fir_func(fir_cont_ty(param_ty));
}

static inline bool is_valid_pointee_ty(const struct fir_node* ty) {
    return is_valid_ty(ty) && ty->tag != FIR_ERR_TY && ty->tag != FIR_MEM_TY;
}

struct fir_node* fir_global(const struct fir_node* ty) {
    assert(is_valid_pointee_ty(ty));
    struct fir_mod* mod = fir_node_mod(ty);
    struct fir_node* global = alloc_node(1);
    global->id = mod->cur_id++;
    global->tag = FIR_GLOBAL;
    global->ty = fir_ptr_ty(mod);
    global->op_count = 1;
    fir_node_set_op(global, 0, fir_bot(ty));
    global_vec_push(&mod->globals, &global);
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

const struct fir_node* fir_int_const(const struct fir_node* ty, uint64_t int_val) {
    assert(ty->tag == FIR_INT_TY);
    int_val &= make_bitmask(ty->data.bitwidth);
    return insert_node(fir_node_mod(ty), &(struct fir_node) {
        .tag = FIR_CONST,
        .ty = ty,
        .data.int_val = int_val
    });
}

const struct fir_node* fir_float_const(const struct fir_node* ty, double float_val) {
    assert(ty->tag == FIR_FLOAT_TY);
    return insert_node(fir_node_mod(ty), &(struct fir_node) {
        .tag = FIR_CONST,
        .ty = ty,
        .data.float_val = float_val
    });
}

const struct fir_node* fir_iarith_op(
    enum fir_node_tag tag,
    const struct fir_node* left,
    const struct fir_node* right)
{
    assert(left->ty == right->ty);
    assert(left->ty->tag == FIR_INT_TY);
    assert(fir_node_tag_is_iarith_op(tag));
    return insert_node(fir_node_mod(left), (const struct fir_node*)&(struct { FIR_NODE(2) }) {
        .tag = tag,
        .op_count = 2,
        .ty = left->ty,
        .ops = { left, right }
    });
}

const struct fir_node* fir_farith_op(
    enum fir_node_tag tag,
    enum fir_fp_flags fp_flags,
    const struct fir_node* left,
    const struct fir_node* right)
{
    assert(left->ty == right->ty);
    assert(left->ty->tag == FIR_FLOAT_TY);
    assert(fir_node_tag_is_farith_op(tag));
    return insert_node(fir_node_mod(left), (const struct fir_node*)&(struct { FIR_NODE(2) }) {
        .tag = tag,
        .data.fp_flags = fp_flags,
        .op_count = 2,
        .ty = left->ty,
        .ops = { left, right }
    });
}

const struct fir_node* fir_idiv_op(
    enum fir_node_tag tag,
    const struct fir_node* err,
    const struct fir_node* left,
    const struct fir_node* right)
{
    assert(err->ty->tag == FIR_ERR_TY);
    assert(left->ty == right->ty);
    assert(left->ty->tag == FIR_INT_TY);
    assert(fir_node_tag_is_idiv_op(tag));
    struct fir_mod* mod = fir_node_mod(left);
    return insert_node(mod, (const struct fir_node*)&(struct { FIR_NODE(3) }) {
        .tag = tag,
        .op_count = 3,
        .ty = fir_tup_ty(mod, (const struct fir_node*[]) { err->ty, left->ty }, 2),
        .ops = { err, left, right }
    });
}

const struct fir_node* fir_fdiv_op(
    enum fir_node_tag tag,
    enum fir_fp_flags fp_flags,
    const struct fir_node* err,
    const struct fir_node* left,
    const struct fir_node* right)
{
    assert(err->ty->tag == FIR_ERR_TY);
    assert(left->ty == right->ty);
    assert(left->ty->tag == FIR_FLOAT_TY);
    assert(fir_node_tag_is_fdiv_op(tag));
    struct fir_mod* mod = fir_node_mod(left);
    return insert_node(mod, (const struct fir_node*)&(struct { FIR_NODE(3) }) {
        .tag = tag,
        .data.fp_flags = fp_flags,
        .op_count = 3,
        .ty = fir_tup_ty(mod, (const struct fir_node*[]) { err->ty, left->ty }, 2),
        .ops = { err, left, right }
    });
}

const struct fir_node* fir_icmp_op(
    enum fir_node_tag tag,
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
        .ops = { left, right }
    });
}

const struct fir_node* fir_fcmp_op(
    enum fir_node_tag tag,
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
        .ops = { left, right }
    });
}

const struct fir_node* fir_bit_op(
    enum fir_node_tag tag,
    const struct fir_node* left,
    const struct fir_node* right)
{
    assert(left->ty == right->ty);
    assert(left->ty->tag == FIR_INT_TY);
    assert(fir_node_tag_is_bit_op(tag));
    return insert_node(fir_node_mod(left), (const struct fir_node*)&(struct { FIR_NODE(2) }) {
        .tag = tag,
        .op_count = 2,
        .ty = left->ty,
        .ops = { left, right }
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

const struct fir_node* fir_cast_op(
    enum fir_node_tag tag,
    const struct fir_node* ty,
    const struct fir_node* arg)
{
    assert(fir_node_tag_is_cast_op(tag));
    assert(is_cast_possible(tag, ty, arg->ty));
    return insert_node(fir_node_mod(ty), (const struct fir_node*)&(struct { FIR_NODE(1) }) {
        .tag = tag,
        .op_count = 1,
        .ty = ty,
        .ops = { arg }
    });
}

static const struct fir_node* infer_tup_ty(
    struct fir_mod* mod,
    const struct fir_node* const* elems,
    size_t elem_count)
{
    const struct fir_node* small_elems_ty[SMALL_OP_COUNT];
    const struct fir_node** elems_ty = small_elems_ty;
    if (elem_count > SMALL_OP_COUNT)
        elems_ty = xmalloc(sizeof(const struct fir_node*) * elem_count);
    for (size_t i = 0; i < elem_count; ++i)
        elems_ty[i] = elems[i]->ty;
    const struct fir_node* tup_ty = fir_tup_ty(mod, elems_ty, elem_count);
    if (elem_count > SMALL_OP_COUNT)
        free(elems_ty);
    return tup_ty;
}

const struct fir_node* fir_tup(
    struct fir_mod* mod,
    const struct fir_node* const* elems,
    size_t elem_count)
{
    if (elem_count == 0)
        return fir_unit(mod);

    struct small_tup { FIR_NODE(SMALL_OP_COUNT) } small_tup = {};
    struct fir_node* tup = (struct fir_node*)&small_tup;
    if (elem_count > SMALL_OP_COUNT)
        tup = alloc_node(elem_count);
    tup->tag = FIR_TUP;
    tup->ty = infer_tup_ty(mod, elems, elem_count);
    tup->op_count = elem_count;
    memcpy(tup->ops, elems, sizeof(struct fir_node*) * elem_count);
    const struct fir_node* result = insert_node(mod, tup);
    if (elem_count > SMALL_OP_COUNT)
        free(tup);
    return result;
}

const struct fir_node* fir_unit(struct fir_mod* mod) {
    return mod->unit;
}

const struct fir_node* fir_array(
    const struct fir_node* ty,
    const struct fir_node* const* elems)
{
    assert(ty->tag == FIR_ARRAY_TY);
    struct fir_mod* mod = fir_node_mod(ty);
    struct small_array { FIR_NODE(SMALL_OP_COUNT) } small_array = {};
    struct fir_node* array = (struct fir_node*)&small_array;
    if (ty->data.array_dim > SMALL_OP_COUNT)
        array = alloc_node(ty->data.array_dim);
    array->tag = FIR_ARRAY;
    array->ty = ty;
    array->op_count = ty->data.array_dim;
    memcpy(array->ops, elems, sizeof(struct fir_node*) * ty->data.array_dim);
    const struct fir_node* result = insert_node(mod, array);
    if (ty->data.array_dim > SMALL_OP_COUNT)
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

const struct fir_node* fir_ext(
    const struct fir_node* aggr,
    const struct fir_node* index)
{
    return insert_node(fir_node_mod(aggr), (const struct fir_node*)&(struct { FIR_NODE(2) }) {
        .tag = FIR_EXT,
        .op_count = 2,
        .ty = infer_ext_ty(aggr->ty, index),
        .ops = { aggr, index }
    });
}

const struct fir_node* fir_ins(
    const struct fir_node* aggr,
    const struct fir_node* index,
    const struct fir_node* elem)
{
    assert(infer_ext_ty(aggr->ty, index) == elem->ty);
    return insert_node(fir_node_mod(aggr), (const struct fir_node*)&(struct { FIR_NODE(3) }) {
        .tag = FIR_INS,
        .op_count = 3,
        .ty = aggr,
        .ops = { aggr, index, elem }
    });
}

const struct fir_node* fir_addrof(
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
        .ops = { ptr, aggr_ty, index }
    });
}

const struct fir_node* fir_select(
    const struct fir_node* cond,
    const struct fir_node* when_true,
    const struct fir_node* when_false)
{
    assert(when_true->ty == when_false->ty);
    const struct fir_node* array_ty = fir_array_ty(when_true->ty, 2);
    const struct fir_node* array = fir_array(array_ty, (const struct fir_node*[]) { when_true, when_false });
    return fir_ext(array, cond);
}

const struct fir_node* fir_call(
    const struct fir_node* callee,
    const struct fir_node* arg)
{
    assert(callee->ty->tag == FIR_FUNC_TY);
    assert(callee->ty->ops[0] == arg->ty);
    return insert_node(fir_node_mod(callee), (const struct fir_node*)&(struct { FIR_NODE(2) }) {
        .tag = FIR_CALL,
        .op_count = 2,
        .ty = callee->ty->ops[1],
        .ops = { callee, arg }
    });
}

const struct fir_node* fir_branch(
    const struct fir_node* cond,
    const struct fir_node* jump_true,
    const struct fir_node* jump_false)
{
    return fir_call(fir_select(cond, jump_true, jump_false), fir_unit(fir_node_mod(cond)));
}

const struct fir_node* fir_param(const struct fir_node* func) {
    assert(func->tag == FIR_FUNC);
    return insert_node(fir_node_mod(func), (const struct fir_node*)&(struct { FIR_NODE(1) }) {
        .tag = FIR_PARAM,
        .op_count = 1,
        .ty = func->ty->ops[0],
        .ops = { func }
    });
}

const struct fir_node* fir_start(const struct fir_node* block) {
    assert(block->tag == FIR_FUNC);
    assert(block->ty->tag == FIR_FUNC_TY);
    assert(block->ty->ops[0]->tag == FIR_FUNC_TY);
    assert(block->ty->ops[1]->tag == FIR_NORET_TY);
    assert(block->ty->ops[0]->ops[1]->tag == FIR_NORET_TY);
    const struct fir_node* ret_ty = block->ty->ops[0]->ops[0];
    return insert_node(fir_node_mod(block), (const struct fir_node*)&(struct { FIR_NODE(1) }) {
        .tag = FIR_START,
        .op_count = 1,
        .ty = ret_ty,
        .ops = { block }
    });
}
