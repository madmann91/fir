#include "macros.h"
#include "support/map.h"

static inline uint32_t hash_int(const int* i) { return *i; }
static inline bool cmp_int(const int* i, const int* j) { return *i == *j; }

MAP_DEFINE(int_map, int, int, hash_int, cmp_int, PRIVATE)

TEST(map) {
    const int n = 100;
    struct int_map int_map = int_map_create();
    for (int i = 0; i < n; ++i)
        REQUIRE(int_map_insert(&int_map, &i, &i));
    REQUIRE(int_map.elem_count == (size_t)n);
    for (int i = 0; i < n; ++i) {
        REQUIRE(int_map_find(&int_map, &i));
        REQUIRE(*int_map_find(&int_map, &i) == i);
    }
    for (int i = 0; i < n; ++i)
        REQUIRE(int_map_remove(&int_map, &i));
    REQUIRE(int_map.elem_count == 0);
    for (int i = 0; i < n; ++i)
        REQUIRE(!int_map_find(&int_map, &i));
    int_map_destroy(&int_map);
}
