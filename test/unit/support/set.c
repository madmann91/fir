#include "macros.h"

#include "support/set.h"

static inline uint32_t hash_int(uint32_t h, const int* i) { return hash_uint32(h, *i); }
static inline bool is_int_equal(const int* i, const int* j) { return *i == *j; }

SET_DEFINE(int_set, int, hash_int, is_int_equal, PRIVATE)

TEST(set) {
    const int n = 100;
    struct int_set int_set = int_set_create();
    for (int i = 0; i < n; ++i)
        REQUIRE(int_set_insert(&int_set, &i));
    REQUIRE(int_set.elem_count == (size_t)n);
    for (int i = 0; i < n; ++i) {
        REQUIRE(int_set_find(&int_set, &i));
        REQUIRE(*int_set_find(&int_set, &i) == i);
    }
    for (int i = 0; i < n; ++i)
        REQUIRE(int_set_remove(&int_set, &i));
    REQUIRE(int_set.elem_count == 0);
    for (int i = 0; i < n; ++i)
        REQUIRE(!int_set_find(&int_set, &i));
    int_set_destroy(&int_set);
}
