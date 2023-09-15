#ifndef FIR_NODE_H
#define FIR_NODE_H

#include "fir/fp_flags.h"
#include "fir/dbg_info.h"
#include "fir/node_list.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct fir_mod;
struct fir_node;

/// \file
///
/// IR nodes can either represent types or values. They are always created via a module, which allows
/// both hash-consing and node simplification to take place on the fly. Nodes have a unique ID, which
/// is given to them by the module, and also reflects the order in which they were created.

/// A tag that identifies the sort of type or value that a node represents.
enum node_tag {
#define x(tag, ...) FIR_##tag,
    FIR_NODE_LIST(x)
#undef x
};

/// A _use_ of a node by another node.
struct fir_use {
    size_t index;                   ///< The operand index where the node is used.
    const struct fir_node* user;    ///< The node which is using the node being considered.
    const struct fir_use* next;     ///< Next use in the list, or NULL.
};

#define FIR_NODE(n) \
    uint64_t id; \
    enum node_tag tag; \
    union { \
        enum fir_fp_flags fp_flags; \
        uint64_t int_val; \
        double float_val; \
        uint32_t bitwidth; \
        uint64_t array_dim; \
    }; \
    const struct fir_use* uses; \
    const struct fir_dbg_info* dbg_info; \
    size_t op_count; \
    union { \
        const struct fir_node* ty; \
        struct fir_mod* mod; \
    }; \
    const struct fir_node* ops[n];

/// \struct fir_node
/// IR node.
struct fir_node { FIR_NODE() };

//-------------------------------------------------------------------------------------------------
/// \name Predicates
//-------------------------------------------------------------------------------------------------

/// \{

bool fir_node_tag_is_ty(enum node_tag);
bool fir_node_tag_is_nominal(enum node_tag);
bool fir_node_tag_is_iarithop(enum node_tag);
bool fir_node_tag_is_farithop(enum node_tag);
bool fir_node_tag_is_icmpop(enum node_tag);
bool fir_node_tag_is_fcmpop(enum node_tag);
bool fir_node_tag_is_idivop(enum node_tag);
bool fir_node_tag_is_fdivop(enum node_tag);
bool fir_node_tag_is_bitop(enum node_tag);
bool fir_node_tag_is_castop(enum node_tag);
bool fir_node_tag_is_aggrop(enum node_tag);
bool fir_node_tag_is_memop(enum node_tag);
bool fir_node_tag_is_controlop(enum node_tag);

static inline bool fir_node_tag_has_fp_flags(enum node_tag tag) { return fir_node_tag_is_fdivop(tag) || fir_node_tag_is_farithop(tag); }
static inline bool fir_node_tag_has_bitwidth(enum node_tag tag) { return tag == FIR_INT_TY || tag == FIR_FLOAT_TY; }

static inline bool fir_node_is_ty(const struct fir_node* n)        { return fir_node_tag_is_ty(n->tag); }
static inline bool fir_node_is_nominal(const struct fir_node* n)   { return fir_node_tag_is_nominal(n->tag); }
static inline bool fir_node_is_iarithop(const struct fir_node* n)  { return fir_node_tag_is_iarithop(n->tag); }
static inline bool fir_node_is_farithop(const struct fir_node* n)  { return fir_node_tag_is_farithop(n->tag); }
static inline bool fir_node_is_icmpop(const struct fir_node* n)    { return fir_node_tag_is_icmpop(n->tag); }
static inline bool fir_node_is_fcmpop(const struct fir_node* n)    { return fir_node_tag_is_fcmpop(n->tag); }
static inline bool fir_node_is_idivop(const struct fir_node* n)    { return fir_node_tag_is_idivop(n->tag); }
static inline bool fir_node_is_fdivop(const struct fir_node* n)    { return fir_node_tag_is_fdivop(n->tag); }
static inline bool fir_node_is_bitop(const struct fir_node* n)     { return fir_node_tag_is_bitop(n->tag); }
static inline bool fir_node_is_castop(const struct fir_node* n)    { return fir_node_tag_is_castop(n->tag); }
static inline bool fir_node_is_aggrop(const struct fir_node* n)    { return fir_node_tag_is_aggrop(n->tag); }
static inline bool fir_node_is_memop(const struct fir_node* n)     { return fir_node_tag_is_memop(n->tag); }
static inline bool fir_node_is_controlop(const struct fir_node* n) { return fir_node_tag_is_controlop(n->tag); }
static inline bool fir_node_has_fp_flags(const struct fir_node* n) { return fir_node_tag_has_fp_flags(n->tag); }
static inline bool fir_node_has_bitwidth(const struct fir_node* n) { return fir_node_tag_has_bitwidth(n->tag); }

/// \}

const char* fir_node_tag_to_string(enum node_tag);
struct fir_mod* fir_node_mod(const struct fir_node*);
const char* fir_node_name(const struct fir_node*);

void fir_node_set_op(struct fir_node* node, size_t op_index, const struct fir_node* op);
void fir_node_print_with_indent(const struct fir_node*, size_t indent);
static inline void fir_node_print(const struct fir_node* n) { fir_node_print_with_indent(n, 0); }

#endif
