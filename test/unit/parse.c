#include "macros.h"

#include <fir/module.h>
#include <fir/node.h>

TEST(parse) {
    const char data[] =
        "int_ty[32] zero = const[0]\n"
        "int_ty[32] one = const[1]\n"
        "int_ty[32] two = iadd (\none ,\tone)\n"
        "tup_ty(int_ty[32], int_ty[32]) pair = tup(one, two)\n"
        "func_ty(int_ty[32], tup_ty(int_ty[32], int_ty[32])) f = func(pair)\n";

    const size_t data_len = sizeof(data) / sizeof(data[0]);

    struct fir_mod* mod = fir_mod_create("module");
    REQUIRE(fir_mod_parse(mod, &(struct fir_parse_input) {
        .file_name = "stdin",
        .file_data = data,
        .file_size = data_len - 1,
        .error_log = stderr
    }));
    fir_mod_destroy(mod);
}

TEST(parse_fail) {
    const char data[] =
        "int_ty[32] zero = const[0]\n"
        "int_ty[32] zero = const[1]\n";

    const size_t data_len = sizeof(data) / sizeof(data[0]);

    struct fir_mod* mod = fir_mod_create("module");
    REQUIRE(!fir_mod_parse(mod, &(struct fir_parse_input) {
        .file_name = "stdin",
        .file_data = data,
        .file_size = data_len - 1,
    }));
    fir_mod_destroy(mod);
}
