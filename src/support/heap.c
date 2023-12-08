#include "heap.h"

#include <string.h>

static inline size_t parent_of(size_t i) {
    return (i - 1) / 2;
}

static inline size_t child_of(size_t i, size_t j) {
    return 2 * i + 1 + j;
}

static inline void* elem_at(void* begin, size_t size, size_t i) {
    return ((char*)begin) + size * i;
}

void heap_push(void* begin, size_t count, size_t size, const void* elem, bool (*less_than)(const void*, const void*)) {
    size_t i = count;
    while (i > 0) {
        const void* parent = elem_at(begin, size, parent_of(i));
        if (less_than(elem, parent))
            break;
        memcpy(elem_at(begin, size, i), parent, size);
        i = parent_of(i);
    }
    memcpy(elem_at(begin, size, i), elem, size);
}

void heap_pop(void* begin, size_t count, size_t size, void* elem, bool (*less_than)(const void*, const void*)) {
    memcpy(elem, begin, size);
    size_t i = 0;
    while (true) {
        size_t left  = child_of(i, 0);
        size_t right = child_of(i, 1);
        size_t largest = count - 1;
        if (left < count && less_than(elem_at(begin, size, largest), elem_at(begin, size, left)))
            largest = left;
        if (right < count && less_than(elem_at(begin, size, largest), elem_at(begin, size, right)))
            largest = right;
        memcpy(elem_at(begin, size, i), elem_at(begin, size, largest), size);
        if (largest == count - 1)
            break;
        i = largest;
    }
}
