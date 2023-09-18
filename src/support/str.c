#include "str.h"
#include "alloc.h"

#include <string.h>
#include <stdlib.h>

struct str_view str_view(const struct str* str) {
    return (struct str_view) { .data = str->data, .length = str->length };
}

struct str str_create(void) {
    return (struct str) {};
}

struct str str_copy(struct str_view view) {
    char* copy = xmalloc(view.length);
    memcpy(copy, view.data, view.length);
    return (struct str) {
        .data = copy,
        .length = view.length,
        .capacity = view.length
    };
}

static inline void make_large_enough(struct str* str, size_t added_bytes) {
    if (str->length + added_bytes > str->capacity) {
        str->capacity += str->capacity >> 1;
        if (str->length + added_bytes > str->capacity)
            str->capacity = str->length + added_bytes;
        str->data = xrealloc(str->data, str->capacity);
    }
}

void str_push(struct str* str, char c) {
    make_large_enough(str, 1);
    str->data[str->length++] = c;
}

void str_append(struct str* str, struct str_view view) {
    make_large_enough(str, view.length);
    memcpy(str->data + str->length, view.data, view.length);
    str->length += view.length;
}

void str_clear(struct str* str) {
    str->length = 0;
}

void str_destroy(struct str* str) {
    free(str->data);
}
