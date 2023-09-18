#pragma once

#include <stddef.h>

struct str_view {
    const char* data;
    size_t length;
};

struct str {
    char* data;
    size_t length;
    size_t capacity;
};

struct str_view str_view(const struct str*);
struct str str_create(void);
struct str str_copy(struct str_view);
void str_push(struct str*, char);
void str_append(struct str*, struct str_view);
void str_clear(struct str*);
static inline void str_terminate(struct str* str) { str_push(str, 0); }
void str_destroy(struct str*);
