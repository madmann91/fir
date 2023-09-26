#pragma once

#include "set.h"
#include "map.h"
#include "alloc.h"
#include "linkage.h"
#include "hash.h"

#include <stddef.h>
#include <stdio.h>
#include <assert.h>

#define GRAPH_DEFINE(name, key_t, hash, cmp, print, linkage) \
    GRAPH_DECL(name, key_t, linkage) \
    GRAPH_IMPL(name, key_t, hash, cmp, print, linkage)

#define GRAPH_DECL(name, key_t, linkage) \
    struct name##_edge; \
    struct name##_node { \
        key_t data; \
        struct name##_edge* ins; \
        struct name##_edge* outs; \
    }; \
    struct name##_edge { \
        const struct name##_node* from; \
        const struct name##_node* to; \
        struct name##_edge* next_in; \
        struct name##_edge* next_out; \
    }; \
    MAP_DECL(name##_node_map, key_t, struct name##_node*, linkage) \
    SET_DECL(name##_edge_set, struct name##_edge*, linkage) \
    struct name { \
        struct name##_node_map nodes; \
        struct name##_edge_set edges; \
    }; \
    LINKAGE(linkage) struct name name##_create(void); \
    LINKAGE(linkage) void name##_destroy(struct name*); \
    LINKAGE(linkage) struct name##_node* name##_insert(struct name*, key_t const*); \
    LINKAGE(linkage) struct name##_edge* name##_connect(struct name*, struct name##_node*, struct name##_node*); \
    LINKAGE(linkage) void name##_print(FILE*, const struct name*); \
    LINKAGE(linkage) void name##_dump(const struct name*);

#define GRAPH_IMPL(name, key_t, hash, cmp, print, linkage) \
    LINKAGE(linkage) uint32_t name##_hash_edge(struct name##_edge* const* edge_ptr) { \
        uint32_t h = hash_init(); \
        h = hash_uint64(h, (uintptr_t)(*edge_ptr)->from); \
        h = hash_uint64(h, (uintptr_t)(*edge_ptr)->to); \
        return h; \
    } \
    LINKAGE(linkage) bool name##_cmp_edge( \
        struct name##_edge* const* edge_ptr, \
        struct name##_edge* const* other_ptr) \
    { \
        return \
            (*edge_ptr)->from == (*other_ptr)->from && \
            (*edge_ptr)->to == (*other_ptr)->to; \
    } \
    MAP_IMPL(name##_node_map, key_t, struct name##_node*, hash, cmp, linkage) \
    SET_IMPL(name##_edge_set, struct name##_edge*, name##_hash_edge, name##_cmp_edge, linkage) \
    LINKAGE(linkage) struct name name##_create(void) { \
        return (struct name) { \
            .nodes = name##_node_map_create(), \
            .edges = name##_edge_set_create() \
        }; \
    } \
    LINKAGE(linkage) void name##_destroy(struct name* graph) { \
        MAP_FOREACH(key_t, key_ptr, struct name##_node*, node_ptr, graph->nodes) { \
            (void)key_ptr; \
            free(*node_ptr); \
        } \
        name##_node_map_destroy(&graph->nodes); \
        SET_FOREACH(struct name##_edge*, edge_ptr, graph->edges) { \
            free(*edge_ptr); \
        } \
        name##_edge_set_destroy(&graph->edges); \
    } \
    LINKAGE(linkage) struct name##_node* name##_insert(struct name* graph, key_t const* data) { \
        struct name##_node* const* found = name##_node_map_find(&graph->nodes, data); \
        if (found) \
            return *found; \
        struct name##_node* node = xcalloc(1, sizeof(struct name##_node)); \
        memcpy(&node->data, data, sizeof(key_t)); \
        [[maybe_unused]] bool was_inserted = name##_node_map_insert(&graph->nodes, data, &node); \
        assert(was_inserted); \
        return node; \
    } \
    LINKAGE(linkage) struct name##_edge* name##_connect( \
        struct name* graph, \
        struct name##_node* from, \
        struct name##_node* to) \
    { \
        struct name##_edge* edge = &(struct name##_edge) { .from = from, .to = to }; \
        struct name##_edge* const* found = name##_edge_set_find(&graph->edges, &edge); \
        if (found) \
            return *found; \
        edge = xmalloc(sizeof(struct name##_edge)); \
        edge->from = from; \
        edge->to = to; \
        edge->next_in = to->ins; \
        edge->next_out = from->outs; \
        to->ins = from->outs = edge; \
        [[maybe_unused]] bool was_inserted = name##_edge_set_insert(&graph->edges, &edge); \
        assert(was_inserted); \
        return edge; \
    } \
    LINKAGE(linkage) void name##_print(FILE* file, const struct name* graph) { \
        fprintf(file, "digraph {\n"); \
        SET_FOREACH(const struct name##_edge*, edge_ptr, graph->edges) { \
            const struct name##_edge* edge = *edge_ptr; \
            fprintf(file, "    "); \
            print(file, &edge->from->data); \
            fprintf(file, "->"); \
            print(file, &edge->to->data); \
            fprintf(file, "\n"); \
        } \
        fprintf(file, "}\n"); \
    } \
    LINKAGE(linkage) void name##_dump(const struct name* graph) { \
        name##_print(stdout, graph); \
        fflush(stdout); \
    }
