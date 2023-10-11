#pragma once

#include <stddef.h>

[[nodiscard]] void* xmalloc(size_t);
[[nodiscard]] void* xcalloc(size_t, size_t);
[[nodiscard]] void* xrealloc(void*, size_t);
