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
    struct fir_node** funcs = fir_mod_funcs(mod);
    REQUIRE(fir_mod_func_count(mod) == 1);
    const struct fir_node* int32_ty = fir_int_ty(mod, 32);
    const struct fir_node* tup_ty = fir_tup_ty(mod, (const struct fir_node*[]) { int32_ty, int32_ty }, 2);
    const struct fir_node* one = fir_int_const(int32_ty, 1);
    const struct fir_node* two = fir_iarith_op(FIR_IADD, one, one);
    const struct fir_node* pair = fir_tup(mod, (const struct fir_node*[]) { one, two }, 2);
    REQUIRE(funcs[0]->ty == fir_func_ty(int32_ty, tup_ty));
    REQUIRE(funcs[0]->ops[0] == pair);
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
