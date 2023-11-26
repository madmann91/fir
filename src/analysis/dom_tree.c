#include "dom_tree.h"

#include <assert.h>
#include <string.h>

static inline size_t intersect_idoms(
    const size_t* idoms,
    size_t idom,
    size_t other_idom,
    [[maybe_unused]] size_t node_count)
{
    while (idom != other_idom) {
        while (idom < other_idom) {
            assert(idom < node_count);
            idom = idoms[idom];
        }
        while (other_idom < idom) {
            assert(other_idom < node_count);
            other_idom = idoms[other_idom];
        }
    }
    return idom;
}

static inline void compute_idoms(
    const struct graph_node_vec* post_order,
    size_t post_order_index,
    size_t* idoms,
    enum graph_dir dir)
{
    static const size_t idom_sentinel = SIZE_MAX;
    size_t node_count = post_order->elem_count;

    // Dominator tree construction based on "A Simple, Fast Dominance Algorithm", by K. D. Cooper et al.
    for (size_t i = 0; i < node_count; ++i)
        idoms[i] = idom_sentinel;

    enum graph_dir reverse_dir = graph_dir_reverse(dir);
    idoms[node_count - 1] = node_count - 1;

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = node_count - 1; i-- > 0;) {
            size_t idom = idom_sentinel;
            GRAPH_FOREACH_EDGE(edge, post_order->elems[i], reverse_dir) {
                size_t other_idom = graph_edge_endpoint(edge, reverse_dir)->data[post_order_index].index;
                if (idoms[other_idom] == idom_sentinel)
                    continue;
                idom = idom == idom_sentinel ? other_idom : intersect_idoms(idoms, idom, other_idom, node_count);
            }
            if (idom != idoms[i]) {
                idoms[i] = idom;
                changed = true;
            }
        }
    }
}

struct dom_tree dom_tree_create(
    const struct graph_node_vec* post_order,
    size_t post_order_index,
    size_t dom_tree_index,
    enum graph_dir dir)
{
    const size_t node_count = post_order->elem_count;
    assert(node_count > 0);
    size_t* idoms = xcalloc(node_count, sizeof(size_t));
    struct dom_tree_node* nodes = xcalloc(node_count, sizeof(struct dom_tree_node));
    compute_idoms(post_order, post_order_index, idoms, dir);

    for (size_t i = node_count; i-- > 0;) {
        post_order->elems[i]->data[dom_tree_index].ptr = &nodes[i];
        nodes[i].idom = post_order->elems[idoms[i]];
        nodes[i].depth = nodes[idoms[i]].depth + 1;
    }

    free(idoms);
    return (struct dom_tree) {
        .nodes = nodes,
        .node_count = node_count
    };
}

void dom_tree_destroy(struct dom_tree* dom_tree) {
    free(dom_tree->nodes);
    memset(dom_tree, 0, sizeof(struct dom_tree));
}
