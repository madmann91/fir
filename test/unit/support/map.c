#include "macros.h"
#include "support/map.h"

static inline uint32_t hash_int(const int* i) { return *i; }
static inline bool cmp_int(const int* i, const int* j) { return *i == *j; }

DEF_MAP(foo_map, int, int, hash_int, cmp_int, PRIVATE)

TEST(map) {
    struct foo_map foo_map = foo_map_create();
    for (int i = 0; i < 100; ++i)
        REQUIRE(foo_map_insert(&foo_map, &i, &i));
    for (int i = 0; i < 100; ++i) {
        REQUIRE(foo_map_find(&foo_map, &i));
        REQUIRE(*foo_map_find(&foo_map, &i) == i);
    }
    foo_map_destroy(&foo_map);
}
