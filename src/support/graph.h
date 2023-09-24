#pragma once

#include <stddef.h>
#include <stdio.h>

struct graph_edge {
    const struct graph_node* from;
    const struct graph_node* to;
    struct graph_edge* next_in;
    struct graph_edge* next_out;
};

struct graph_node {
    void* data;
    struct graph_edge* ins;
    struct graph_edge* outs;
};

struct graph;

struct graph* graph_create(void);
void graph_destroy(struct graph*);

struct graph_node* graph_insert(struct graph*, void*);

struct graph_edge* graph_connect(
    struct graph*,
    struct graph_node* from,
    struct graph_node* to);

void graph_print(FILE*, const struct graph*, void (*print_data)(FILE*, void*));
