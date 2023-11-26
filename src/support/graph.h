#pragma once

#include "vec.h"
#include "set.h"
#include "map.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define GRAPH_FOREACH_EDGE(edge, node, dir) \
    for (struct graph_edge* edge = graph_node_first_edge(node, dir); edge; \
        edge = graph_edge_next(edge, dir))

#define GRAPH_FOREACH_OUTGOING_EDGE(edge, node) \
    GRAPH_FOREACH_EDGE(edge, node, GRAPH_DIR_FORWARD)

#define GRAPH_FOREACH_INCOMING_EDGE(edge, node) \
    GRAPH_FOREACH_EDGE(edge, node, GRAPH_DIR_BACKWARD)

struct graph_node;

struct graph_edge {
    struct graph_node* from;
    struct graph_node* to;
    struct graph_edge* next_in;
    struct graph_edge* next_out;
};

enum graph_node_id {
    GRAPH_SOURCE_ID,
    GRAPH_SINK_ID,
    GRAPH_OTHER_ID
};

union graph_node_data {
    void* ptr;
    size_t index;
};

struct graph_node {
    uint64_t id;
    void* key;
    struct graph_edge* ins;
    struct graph_edge* outs;
    union graph_node_data data[];
};

SET_DECL(graph_edge_set, struct graph_edge*, PUBLIC)
VEC_DECL(graph_node_vec, struct graph_node*, PUBLIC)
MAP_DECL(graph_node_key_map, void*, struct graph_node*, PUBLIC)
SET_DECL(graph_node_set, struct graph_node*, PUBLIC)

struct graph {
    uint64_t cur_id;
    size_t data_size;
    struct graph_node* source;
    struct graph_node* sink;
    struct graph_node_key_map nodes;
    struct graph_edge_set edges;
};

enum graph_dir {
    GRAPH_DIR_FORWARD,
    GRAPH_DIR_BACKWARD
};

[[nodiscard]] enum graph_dir graph_dir_reverse(enum graph_dir);
[[nodiscard]] struct graph_edge* graph_node_first_edge(const struct graph_node*, enum graph_dir);
[[nodiscard]] struct graph_edge* graph_edge_next(const struct graph_edge*, enum graph_dir);
[[nodiscard]] struct graph_node* graph_edge_endpoint(const struct graph_edge*, enum graph_dir);
[[nodiscard]] struct graph_node* graph_source(struct graph*, enum graph_dir);
[[nodiscard]] struct graph_node* graph_sink(struct graph*, enum graph_dir);

[[nodiscard]] struct graph graph_create(size_t);
void graph_destroy(struct graph*);

struct graph_node* graph_insert(struct graph*, void* key);
struct graph_edge* graph_connect(struct graph*, struct graph_node*, struct graph_node*);

struct graph_node_vec graph_compute_post_order(struct graph*, enum graph_dir);
struct graph_node_vec graph_compute_depth_first_order(struct graph*, enum graph_dir);

void graph_print(FILE*, const struct graph*);
void graph_dump(const struct graph*);
