#include "macros.h"
#include "support/set.h"

static inline uint32_t hash_int(const int* i) { return *i; }
static inline bool cmp_int(const int* i, const int* j) { return *i == *j; }

DEF_SET(foo_set, int, hash_int, cmp_int, PRIVATE)

TEST(set) {
    struct foo_set foo_set = foo_set_create();
    for (int i = 0; i < 100; ++i)
        REQUIRE(foo_set_insert(&foo_set, &i));
    for (int i = 0; i < 100; ++i) {
        REQUIRE(foo_set_find(&foo_set, &i));
        REQUIRE(*foo_set_find(&foo_set, &i) == i);
    }
    foo_set_destroy(&foo_set);
}
