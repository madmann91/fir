#pragma once

#include <stddef.h>
#include <stdio.h>

struct mem_stream {
    FILE* file;
    char* buf;
    size_t size;
};

void mem_stream_init(struct mem_stream*);
void mem_stream_destroy(struct mem_stream*);
void mem_stream_flush(struct mem_stream*);
