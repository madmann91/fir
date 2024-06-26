#include "loop_tree.h"

#include <overture/union_find.h>
#include <overture/vec.h>
#include <overture/set.h>

#include <string.h>

static inline uint32_t hash_index(uint32_t h, const size_t* index_ptr) {
    return hash_uint64(h, *index_ptr);
}

static inline bool is_index_equal(const size_t* index_ptr, const size_t* other_ptr) {
    return *index_ptr == *other_ptr;
}

VEC_DEFINE(index_vec, size_t, PRIVATE)
SET_DEFINE(index_set, size_t, hash_index, is_index_equal, PRIVATE)

static inline size_t* compute_last_descendants(
    const struct graph_node_vec* depth_first_order,
    size_t depth_first_order_index,
    enum graph_dir dir)
{
    // Compute the depth-first index of the last descendant of each node in the depth first search
    // tree represented by `depth_first_order`.
    size_t* last_descendants = xcalloc(depth_first_order->elem_count, sizeof(size_t));
    for (size_t i = depth_first_order->elem_count; i-- > 0;) {
        const struct graph_node* node = depth_first_order->elems[i];
        size_t last_descendant = i;
        GRAPH_FOREACH_EDGE(edge, node, dir) {
            size_t descendant = last_descendants[graph_edge_endpoint(edge, dir)->user_data[depth_first_order_index].index];
            last_descendant = last_descendant < descendant ? descendant : last_descendant;
        }
        last_descendants[i] = last_descendant;
    }
    return last_descendants;
}

static inline bool is_ancestor(const size_t* last_descendants, size_t i, size_t j) {
    return i < j && j <= last_descendants[i];
}

struct loop_tree loop_tree_create(
    const struct graph_node_vec* depth_first_order,
    size_t depth_first_order_index,
    size_t loop_tree_index,
    enum graph_dir dir)
{
    // This is inspired from P. Havlak's "Nesting of Reducible and Irreducible Loops".
    size_t* last_descendants = compute_last_descendants(depth_first_order, depth_first_order_index, dir);
    const size_t node_count = depth_first_order->elem_count;

    size_t* headers                  = xcalloc(node_count, sizeof(size_t));
    struct loop_tree_node* nodes     = xcalloc(node_count, sizeof(struct loop_tree_node));
    struct index_vec* back_preds     = xmalloc(node_count * sizeof(struct index_vec));
    struct index_vec* non_back_preds = xmalloc(node_count * sizeof(struct index_vec));

    enum graph_dir reverse_dir = graph_dir_reverse(dir);

    for (size_t i = 0; i < node_count; ++i) {
        headers[i] = i;
        back_preds[i] = index_vec_create();
        non_back_preds[i] = index_vec_create();
        depth_first_order->elems[i]->user_data[loop_tree_index].ptr = &nodes[i];

        const struct graph_node* node = depth_first_order->elems[i];
        GRAPH_FOREACH_EDGE(edge, node, reverse_dir) {
            const struct graph_node* endpoint = graph_edge_endpoint(edge, reverse_dir);
            size_t endpoint_index = endpoint->user_data[depth_first_order_index].index;
            bool is_back_pred = is_ancestor(last_descendants, i, endpoint_index);
            index_vec_push(is_back_pred ? &back_preds[i] : &non_back_preds[i], &endpoint_index);
        }
    }

    struct index_set loop_body = index_set_create();
    struct index_vec worklist = index_vec_create();
    for (size_t i = node_count; i-- > 0;) {
        assert(worklist.elem_count == 0);

        index_set_clear(&loop_body);
        VEC_FOREACH(size_t, back_pred, back_preds[i]) {
            if (i == *back_pred) {
                nodes[i].type = LOOP_SELF;
            } else {
                size_t pred_header = union_find(headers, *back_pred);
                if (index_set_insert(&loop_body, &pred_header))
                    index_vec_push(&worklist, &pred_header);
            }
        }

        if (loop_body.elem_count != 0)
            nodes[i].type = LOOP_REDUCIBLE;

        while (worklist.elem_count > 0) {
            size_t node = *index_vec_pop(&worklist);

            VEC_FOREACH(size_t, non_back_pred, non_back_preds[node]) {
                size_t header = union_find(headers, *non_back_pred);
                if (!is_ancestor(last_descendants, i, header)) {
                    nodes[i].type = LOOP_IRREDUCIBLE;
                    index_vec_push(&non_back_preds[i], &header);
                } else if (header != i && index_set_insert(&loop_body, &header)) {
                    index_vec_push(&worklist, &header);
                }
            }
        }

        SET_FOREACH(size_t, node, loop_body) {
            union_merge(headers, *node, i);
        }
    }
    index_set_destroy(&loop_body);
    index_vec_destroy(&worklist);

    for (size_t i = 0; i < node_count; ++i) {
        index_vec_destroy(&back_preds[i]);
        index_vec_destroy(&non_back_preds[i]);
        size_t header = headers[i] == i ? 0 : headers[i];
        nodes[i].parent = depth_first_order->elems[header];
        nodes[i].depth = nodes[header].depth + 1;
        nodes[i].loop_depth = nodes[header].loop_depth + (nodes[i].type != LOOP_NONHEADER ? 1 : 0);
    }

    free(headers);
    free(back_preds);
    free(non_back_preds);
    free(last_descendants);
    return (struct loop_tree) {
        .nodes = nodes,
        .node_count = depth_first_order->elem_count
    };
}

void loop_tree_destroy(struct loop_tree* loop_tree) {
    free(loop_tree->nodes);
    memset(loop_tree, 0, sizeof(struct loop_tree));
}
