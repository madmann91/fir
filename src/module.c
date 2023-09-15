#include "fir/module.h"
#include "fir/node.h"

#include "support/map.h"
#include "support/bits.h"
#include "support/vec.h"
#include "support/alloc.h"
#include "support/hash.h"
#include "simplify.h"

#define SMALL_OP_COUNT 8

static uint32_t hash_node_data(uint32_t h, const struct fir_node* node) {
    if (fir_node_has_fp_flags(node))
        h = hash_uint32(h, node->fp_flags);
    else if (node->tag == FIR_ARRAY_TY)
        h = hash_uint64(h, node->array_dim);
    else if (node->tag == FIR_CONST && node->ty->tag == FIR_INT_TY)
        h = hash_uint64(h, node->int_val);
    else if (node->tag == FIR_CONST && node->ty->tag == FIR_FLOAT_TY)
        h = hash_uint64(h, double_bits(node->float_val));
    else if (fir_node_has_bitwidth(node))
        h = hash_uint32(h, node->bitwidth);
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
        return node->fp_flags == other->fp_flags;
    else if (node->tag == FIR_CONST && node->ty->tag == FIR_INT_TY)
        return node->int_val == other->int_val;
    else if (node->tag == FIR_CONST && node->ty->tag == FIR_FLOAT_TY)
        return double_bits(node->float_val) == double_bits(other->float_val);
    else if (node->tag == FIR_ARRAY_TY)
        return node->array_dim == other->array_dim;
    else if (fir_node_has_bitwidth(node))
        return node->bitwidth == other->bitwidth;
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

DECL_MAP(internal_node_map, const struct fir_node*, const struct fir_node*, hash_node, cmp_node)
DECL_VEC(func_vec, struct fir_node*)
DECL_VEC(global_vec, struct fir_node*)

struct fir_mod {
    char* name;
    uint64_t cur_id;
    struct func_vec funcs;
    struct global_vec globals;
    struct internal_node_map nodes;
    const struct fir_node* mem;
    const struct fir_node* err;
    const struct fir_node* noret;
    const struct fir_node* ptr;
    const struct fir_node* unit_ty;
    const struct fir_node* unit;
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
    new_node->id = mod->cur_id++;
    memcpy(new_node, node, sizeof(struct fir_node) + sizeof(struct fir_node*) * node->op_count);
    for (size_t i = 0; i < node->op_count; ++i)
        record_use(new_node, i);
    const struct fir_node* simplified_node = simplify_node(mod, new_node);
    internal_node_map_insert(&mod->nodes, (const struct fir_node*const*)&new_node, &simplified_node);
    return new_node;
}

struct fir_mod* fir_mod_create(const char* name) {
    struct fir_mod* mod = xcalloc(1, sizeof(struct fir_mod));
    mod->name = strdup(name);
    mod->cur_id = 0;
    mod->nodes   = internal_node_map_create();
    mod->mem     = insert_node(mod, &(struct fir_node) { .tag = FIR_MEM_TY,   .mod = mod });
    mod->err     = insert_node(mod, &(struct fir_node) { .tag = FIR_ERR_TY,   .mod = mod });
    mod->noret   = insert_node(mod, &(struct fir_node) { .tag = FIR_NORET_TY, .mod = mod });
    mod->ptr     = insert_node(mod, &(struct fir_node) { .tag = FIR_PTR_TY,   .mod = mod });
    mod->unit_ty = insert_node(mod, &(struct fir_node) { .tag = FIR_TUP_TY,   .mod = mod });
    mod->unit    = insert_node(mod, &(struct fir_node) { .tag = FIR_TUP,      .ty = mod->unit_ty });
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

struct fir_node** fir_mod_funcs(struct fir_mod* mod) {
    return mod->funcs.elems;
}

struct fir_node** fir_mod_globals(struct fir_mod* mod) {
    return mod->globals.elems;
}

size_t fir_mod_func_count(struct fir_mod* mod) {
    return mod->funcs.elem_count;
}

size_t fir_mod_global_count(struct fir_mod* mod) {
    return mod->globals.elem_count;
}

const struct fir_node* fir_mem_ty(struct fir_mod* mod) { return mod->mem; }
const struct fir_node* fir_err_ty(struct fir_mod* mod) { return mod->err; }
const struct fir_node* fir_noret_ty(struct fir_mod* mod) { return mod->noret; }
const struct fir_node* fir_ptr_ty(struct fir_mod* mod) { return mod->ptr; }

const struct fir_node* fir_array_ty(const struct fir_node* elem_ty, size_t size) {
    struct fir_mod* mod = fir_node_mod(elem_ty);
    return insert_node(mod, (const struct fir_node*)&(struct { FIR_NODE(1) }) {
        .tag = FIR_ARRAY_TY,
        .mod = mod,
        .array_dim = size,
        .op_count = 1,
        .ops = { elem_ty }
    });
}

const struct fir_node* fir_dynarray_ty(const struct fir_node* elem_ty) {
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
        .bitwidth = bitwidth
    });
}

const struct fir_node* fir_float_ty(struct fir_mod* mod, uint32_t bitwidth) {
    return insert_node(mod, &(struct fir_node) {
        .tag = FIR_FLOAT_TY,
        .mod = mod,
        .bitwidth = bitwidth
    });
}

const struct fir_node* fir_tup_ty(struct fir_mod* mod, const struct fir_node*const* args, size_t arg_count) {
    if (arg_count == 0)
        return mod->unit_ty;

    struct small_tup { FIR_NODE(SMALL_OP_COUNT) } small_tup = {};
    struct fir_node* tup = (struct fir_node*)&small_tup;
    if (arg_count > SMALL_OP_COUNT)
        tup = alloc_node(arg_count);
    tup->tag = FIR_TUP_TY;
    tup->mod = mod;
    tup->op_count = arg_count;
    memcpy(tup->ops, args, sizeof(struct fir_node*) * arg_count);
    const struct fir_node* result = insert_node(mod, tup);
    if (arg_count > SMALL_OP_COUNT)
        free(tup);
    return result;
}

const struct fir_node* fir_func_ty(
    const struct fir_node* param_ty,
    const struct fir_node* ret_ty)
{
    struct fir_mod* mod = fir_node_mod(param_ty);
    return insert_node(mod, (const struct fir_node*)&(struct { FIR_NODE(2) }) {
        .tag = FIR_FUNC_TY,
        .mod = mod,
        .op_count = 2,
        .ops = { param_ty, ret_ty }
    });
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

struct fir_node* fir_block(const struct fir_node* param_ty) {
    return fir_func(fir_func_ty(param_ty, fir_noret_ty(fir_node_mod(param_ty))));
}

struct fir_node* fir_global(const struct fir_node* ty) {
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
    return insert_node(fir_node_mod(ty), &(struct fir_node) {
        .tag = FIR_TOP,
        .ty = ty,
    });
}

const struct fir_node* fir_bot(const struct fir_node* ty) {
    return insert_node(fir_node_mod(ty), &(struct fir_node) {
        .tag = FIR_BOT,
        .ty = ty,
    });
}

const struct fir_node* fir_int_const(const struct fir_node* ty, uint64_t int_val) {
    assert(ty->tag == FIR_INT_TY);
    int_val &= make_bitmask(ty->bitwidth);
    return insert_node(fir_node_mod(ty), &(struct fir_node) {
        .tag = FIR_CONST,
        .ty = ty,
        .int_val = int_val
    });
}

const struct fir_node* fir_float_const(const struct fir_node* ty, double float_val) {
    assert(ty->tag == FIR_FLOAT_TY);
    return insert_node(fir_node_mod(ty), &(struct fir_node) {
        .tag = FIR_CONST,
        .ty = ty,
        .float_val = float_val
    });
}
