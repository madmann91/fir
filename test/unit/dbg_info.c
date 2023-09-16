#include "macros.h"

#include <fir/dbg_info.h>

TEST(dbg_info) {
    struct fir_dbg_info_pool* pool = fir_dbg_info_pool_create();
    const struct fir_dbg_info* dbg_info_1 =
        fir_dbg_info(pool, "foo", "foo.c", (struct fir_source_range) {
            .begin = { .row = 1, .col = 1 },
            .end   = { .row = 2, .col = 1 }
        });
    const struct fir_dbg_info* dbg_info_2 =
        fir_dbg_info(pool, "foo", "bar.c", (struct fir_source_range) {
            .begin = { .row = 3, .col = 1 },
            .end   = { .row = 4, .col = 1 }
        });
    const struct fir_dbg_info* dbg_info_3 =
        fir_dbg_info(pool, "bar", "foo.c", (struct fir_source_range) {
            .begin = { .row = 5, .col = 1 },
            .end   = { .row = 6, .col = 1 }
        });
    REQUIRE(dbg_info_1->name == dbg_info_2->name);
    REQUIRE(dbg_info_1->name != dbg_info_3->name);
    REQUIRE(dbg_info_1->file_name != dbg_info_2->file_name);
    REQUIRE(dbg_info_1->file_name == dbg_info_3->file_name);
    fir_dbg_info_pool_destroy(pool);
}
