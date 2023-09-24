#include "graph.h"
#include "set.h"
#include "alloc.h"
#include "hash.h"

#include <stdint.h>

static inline uint32_t hash_graph_node(struct graph_node* const* node_ptr) {
    return hash_uint64(hash_init(), (uintptr_t)(*node_ptr)->data);
}

static inline bool cmp_graph_node(
    struct graph_node* const* node_ptr,
    struct graph_node* const* other_ptr)
{
    return (*node_ptr)->data == (*other_ptr)->data;
}

static inline uint32_t hash_graph_edge(struct graph_edge* const* edge_ptr) {
    uint32_t h = hash_init();
    h = hash_uint64(h, (uintptr_t)(*edge_ptr)->from);
    h = hash_uint64(h, (uintptr_t)(*edge_ptr)->to);
    return h;
}

static inline bool cmp_graph_edge(
    struct graph_edge* const* edge_ptr,
    struct graph_edge* const* other_ptr)
{
    return
        (*edge_ptr)->from == (*other_ptr)->from &&
        (*edge_ptr)->to == (*other_ptr)->to;
}

DEF_SET(graph_node_set, struct graph_node*, hash_graph_node, cmp_graph_node, PRIVATE)
DEF_SET(graph_edge_set, struct graph_edge*, hash_graph_edge, cmp_graph_edge, PRIVATE)

struct graph {
    struct graph_node_set nodes;
    struct graph_edge_set edges;
};

struct graph* graph_create(void) {
    struct graph* graph = xmalloc(sizeof(struct graph));
    graph->nodes = graph_node_set_create();
    graph->edges = graph_edge_set_create();
    return graph;
}

void graph_destroy(struct graph* graph) {
    FOREACH_SET(struct graph_node*, node_ptr, graph->nodes) {
        free(*node_ptr);
    }
    graph_node_set_destroy(&graph->nodes);
    FOREACH_SET(struct graph_edge*, edge_ptr, graph->edges) {
        free(*edge_ptr);
    }
    graph_edge_set_destroy(&graph->edges);
    free(graph);
}

struct graph_node* graph_insert(struct graph* graph, void* data) {
    struct graph_node* node = &(struct graph_node) { .data = data };
    struct graph_node* const* found = graph_node_set_find(&graph->nodes, &node);
    if (found)
        return *found;

    node = xcalloc(1, sizeof(struct graph_node));
    node->data = data;
    [[maybe_unused]] bool was_inserted = graph_node_set_insert(&graph->nodes, &node);
    assert(was_inserted);
    return node;
}

struct graph_edge* graph_connect(
    struct graph* graph,
    struct graph_node* from,
    struct graph_node* to)
{
    struct graph_edge* edge = &(struct graph_edge) { .from = from, .to = to };
    struct graph_edge* const* found = graph_edge_set_find(&graph->edges, &edge);
    if (found)
        return *found;

    edge = xmalloc(sizeof(struct graph_edge));
    edge->from = from;
    edge->to = to;
    edge->next_in = to->ins;
    edge->next_out = from->outs;
    to->ins = edge;
    from->outs = edge;
    [[maybe_unused]] bool was_inserted = graph_edge_set_insert(&graph->edges, &edge);
    assert(was_inserted);
    return edge;
}

void graph_print(FILE* file, const struct graph* graph, void (*print_data)(FILE*, void*)) {
    fprintf(file, "digraph {\n");
    FOREACH_SET(const struct graph_edge*, edge_ptr, graph->edges) {
        const struct graph_edge* edge = *edge_ptr;
        fprintf(file, "    ");
        print_data(file, edge->from->data);
        fprintf(file, "->");
        print_data(file, edge->to->data);
        fprintf(file, "\n");
    }
    fprintf(file, "}\n");
}
