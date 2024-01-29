#pragma once

#include <stddef.h>

struct delayed_free {
    void** blocks;
    size_t block_count;
};

[[nodiscard]] void* xmalloc(size_t);
[[nodiscard]] void* xcalloc(size_t, size_t);
[[nodiscard]] void* xrealloc(void*, size_t);
