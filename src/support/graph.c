#include "graph.h"
#include "hash.h"

#include <inttypes.h>
#include <assert.h>
#include <string.h>

static inline uint32_t hash_graph_edge(uint32_t h, struct graph_edge* const* edge_ptr) {
    h = hash_uint64(h, (*edge_ptr)->from->id);
    h = hash_uint64(h, (*edge_ptr)->to->id);
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

static inline uint32_t hash_graph_node(uint32_t h, struct graph_node* const* node_ptr) {
    return hash_uint64(h, (uintptr_t)(*node_ptr)->key);
}

static inline bool cmp_graph_node(
    struct graph_node* const* node_ptr,
    struct graph_node* const* other_ptr)
{
    return (*node_ptr)->key == (*other_ptr)->key;
}

static inline uint32_t hash_raw_ptr(uint32_t h, void* const* ptr) {
    return hash_uint64(h, (uintptr_t)*ptr);
}

static inline bool cmp_raw_ptr(void* const* ptr, void* const* other) {
    return *ptr == *other;
}

SET_IMPL(graph_node_set, struct graph_node*, hash_graph_node, cmp_graph_node, PUBLIC)
SET_IMPL(graph_edge_set, struct graph_edge*, hash_graph_edge, cmp_graph_edge, PUBLIC)
MAP_IMPL(graph_node_map, void*, struct graph_node*, hash_raw_ptr, cmp_raw_ptr, PUBLIC)
VEC_IMPL(graph_node_vec, struct graph_node*, PUBLIC)

enum graph_dir graph_dir_reverse(enum graph_dir dir) {
    return dir == GRAPH_DIR_FORWARD ? GRAPH_DIR_BACKWARD : GRAPH_DIR_FORWARD;
}

struct graph_edge* graph_node_first_edge(const struct graph_node* node, enum graph_dir dir) {
    return dir == GRAPH_DIR_FORWARD ? node->outs : node->ins;
}

struct graph_edge* graph_edge_next(const struct graph_edge* edge, enum graph_dir dir) {
    return dir == GRAPH_DIR_FORWARD ? edge->next_out : edge->next_in;
}

struct graph_node* graph_edge_endpoint(const struct graph_edge* edge, enum graph_dir dir) {
    return dir == GRAPH_DIR_FORWARD ? edge->to : edge->from;
}

struct graph_node* graph_source(struct graph* graph, enum graph_dir dir) {
    return dir == GRAPH_DIR_FORWARD ? graph->source : graph->sink;
}

struct graph_node* graph_sink(struct graph* graph, enum graph_dir dir) {
    return graph_source(graph, graph_dir_reverse(dir));
}

size_t graph_node_post_order(const struct graph_node* node, enum graph_dir dir) {
    return dir == GRAPH_DIR_FORWARD ? node->post_order : node->rev_post_order;
}

static inline struct graph_node* alloc_graph_node(size_t user_data_count) {
    return xcalloc(1, sizeof(struct graph_node) + user_data_count * sizeof(void*));
}

struct graph graph_create(size_t user_data_count) {
    struct graph_node* source = alloc_graph_node(user_data_count);
    struct graph_node* sink   = alloc_graph_node(user_data_count);
    source->id = GRAPH_SOURCE_ID;
    sink->id = GRAPH_SINK_ID;
    return (struct graph) {
        .source = source,
        .sink = sink,
        .cur_id = GRAPH_OTHER_ID,
        .user_data_count = user_data_count,
        .post_order = graph_node_vec_create(),
        .depth_first_order = graph_node_vec_create(),
        .nodes = graph_node_map_create(),
        .edges = graph_edge_set_create()
    };
}

void graph_destroy(struct graph* graph) {
    free(graph->source);
    free(graph->sink);
    graph_node_vec_destroy(&graph->post_order);
    graph_node_vec_destroy(&graph->depth_first_order);

    MAP_FOREACH(void*, key_ptr, struct graph_node*, node_ptr, graph->nodes) {
        (void)key_ptr;
        free(*node_ptr);
    }
    graph_node_map_destroy(&graph->nodes);

    SET_FOREACH(struct graph_edge*, edge_ptr, graph->edges) {
        free(*edge_ptr);
    }
    graph_edge_set_destroy(&graph->edges);
    memset(graph, 0, sizeof(struct graph));
}

struct graph_node* graph_insert(struct graph* graph, void* key) {
    struct graph_node* const* node_ptr = graph_node_map_find(&graph->nodes, &key);
    if (node_ptr)
        return *node_ptr;

    struct graph_node* node = alloc_graph_node(graph->user_data_count);
    node->id = graph->cur_id++;
    node->key = key;
    [[maybe_unused]] bool was_inserted = graph_node_map_insert(&graph->nodes, &key, &node);
    assert(was_inserted);
    return node;
}

struct graph_edge* graph_connect(
    struct graph* graph,
    struct graph_node* from,
    struct graph_node* to)
{
    assert(from != graph->sink);
    assert(to != graph->source);

    struct graph_edge* edge = &(struct graph_edge) { .from = from, .to = to };
    struct graph_edge* const* edge_ptr = graph_edge_set_find(&graph->edges, &edge);
    if (edge_ptr)
        return *edge_ptr;

    edge = xmalloc(sizeof(struct graph_edge));
    edge->from = from;
    edge->to = to;
    edge->next_in = to->ins;
    edge->next_out = from->outs;
    to->ins = from->outs = edge;
    [[maybe_unused]] bool was_inserted = graph_edge_set_insert(&graph->edges, &edge);
    assert(was_inserted);
    return edge;
}

void graph_compute_post_order(struct graph* graph, enum graph_dir dir) {
    struct graph_node_set visited_set = graph_node_set_create();
    struct graph_node_vec post_order = graph_node_vec_create();
    struct graph_node_vec stack = graph_node_vec_create();
    struct graph_node* source = graph_source(graph, dir);
    graph_node_vec_push(&stack, &source);
    graph_node_set_insert(&visited_set, &source);
    graph_node_vec_clear(&graph->post_order);
    graph_node_vec_push(&graph->post_order, &source);
restart:
    while (stack.elem_count > 0) {
        struct graph_node* node = stack.elems[stack.elem_count - 1];
        for (struct graph_edge* edge = graph_node_first_edge(node, dir); edge; edge = graph_edge_next(edge, dir)) {
            struct graph_node* target = graph_edge_endpoint(edge, dir);
            if (graph_node_set_insert(&visited_set, &target)) {
                graph_node_vec_push(&stack, &target);
                goto restart;
            }
        }
        graph_node_vec_push(&post_order, &node);
        graph_node_vec_pop(&stack);
    }
    graph_node_vec_destroy(&stack);
    graph_node_set_destroy(&visited_set);

    for (size_t i = 0; i < post_order.elem_count; ++i) {
        post_order.elems[i]->post_order = i;
        post_order.elems[i]->rev_post_order = post_order.elem_count - 1 - i;
    }
}

void graph_compute_depth_first_order(struct graph* graph, enum graph_dir dir) {
    struct graph_node_set visited_set = graph_node_set_create();
    struct graph_node_vec stack = graph_node_vec_create();
    struct graph_node* source = graph_source(graph, dir);
    graph_node_vec_push(&stack, &source);
    graph_node_set_insert(&visited_set, &source);
    graph_node_vec_clear(&graph->depth_first_order);
    graph_node_vec_push(&graph->depth_first_order, &source);
restart:
    while (stack.elem_count > 0) {
        struct graph_node* node = stack.elems[stack.elem_count - 1];
        for (struct graph_edge* edge = graph_node_first_edge(node, dir); edge; edge = graph_edge_next(edge, dir)) {
            struct graph_node* target = graph_edge_endpoint(edge, dir);
            if (graph_node_set_insert(&visited_set, &target)) {
                graph_node_vec_push(&stack, &target);
                graph_node_vec_push(&graph->depth_first_order, &target);
                goto restart;
            }
        }
        graph_node_vec_pop(&stack);
    }
    graph_node_vec_destroy(&stack);
    graph_node_set_destroy(&visited_set);

    for (size_t i = 0; i < graph->depth_first_order.elem_count; ++i)
        graph->depth_first_order.elems[i]->depth_first_order = i;
}

struct graph_node* graph_post_order(struct graph* graph, size_t index, enum graph_dir dir) {
    assert(index < graph->post_order.elem_count);
    return dir == GRAPH_DIR_FORWARD
        ? graph->post_order.elems[index]
        : graph->post_order.elems[graph->post_order.elem_count - index];
}

struct graph_node* graph_depth_first_order(struct graph* graph, size_t index, enum graph_dir dir) {
    assert(index < graph->depth_first_order.elem_count);
    return dir == GRAPH_DIR_FORWARD
        ? graph->depth_first_order.elems[index]
        : graph->depth_first_order.elems[graph->depth_first_order.elem_count - index];
}

static inline void print_node(FILE* file, const struct graph_node* node) {
    if (node->id == GRAPH_SOURCE_ID)
        fprintf(file, "source");
    else if (node->id == GRAPH_SINK_ID)
        fprintf(file, "sink");
    else
        fprintf(file, "%"PRIu64, node->id);
}

void graph_print(FILE* file, const struct graph* graph) {
    fprintf(file, "digraph {\n");
    SET_FOREACH(const struct graph_edge*, edge_ptr, graph->edges) {
        const struct graph_edge* edge = *edge_ptr;
        fprintf(file, "    ");
        print_node(file, edge->from);
        fprintf(file, " -> ");
        print_node(file, edge->to);
        fprintf(file, "\n");
    }
    fprintf(file, "}\n");
}

void graph_dump(const struct graph* graph) {
    graph_print(stdout, graph);
    fflush(stdout);
}
