#pragma once

#include <stddef.h>
#include <stdalign.h>

struct mem_block;

struct mem_pool {
    struct mem_block* first;
    struct mem_block* cur;
};

#define MEM_POOL_ALLOC(pool, T) mem_pool_alloc(&(pool), sizeof(T), alignof(T))

[[nodiscard]] struct mem_pool mem_pool_create(void);
void mem_pool_destroy(struct mem_pool*);
void mem_pool_reset(struct mem_pool*);
void* mem_pool_alloc(struct mem_pool*, size_t size, size_t align);
