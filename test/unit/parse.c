#include "macros.h"

#include <fir/module.h>
#include <fir/node.h>

#include <string.h>

TEST(parse) {
    const char data[] =
        "int_ty[32] zero = const[0]\n"
        "int_ty[32] one = const[1]\n"
        "int_ty[32] two = iadd (\none ,\tone)\n"
        "tup_ty(int_ty[32], int_ty[32]) pair = tup(one, two)\n"
        "func_ty(int_ty[32], tup_ty(int_ty[32], int_ty[32])) f = func(pair)\n";

    struct fir_mod* mod = fir_mod_create("module");
    REQUIRE(fir_mod_parse(mod, &(struct fir_parse_input) {
        .file_name = "stdin",
        .file_data = data,
        .file_size = strlen(data),
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

TEST(parse_redef) {
    const char data[] =
        "int_ty[32] zero = const[0]\n"
        "int_ty[32] zero = const[1]\n";

    struct fir_mod* mod = fir_mod_create("module");
    REQUIRE(!fir_mod_parse(mod, &(struct fir_parse_input) {
        .file_name = "stdin",
        .file_data = data,
        .file_size = strlen(data),
        .error_log = NULL // silence errors
    }));
    fir_mod_destroy(mod);
}

TEST(parse_fp_flags) {
    const char data[] =
        "func_ty(float_ty[32], float_ty[32]) f = func(add_one)\n"
        "float_ty[32] one = const[1.]\n"
        "float_ty[32] x = param(f)\n"
        "float_ty[32] add_one = fadd[+nsz+a](x, one)\n"
        "func_ty(float_ty[32], float_ty[32]) g = func(sub_one)\n"
        "float_ty[32] minus_one = const[-1.]\n"
        "float_ty[32] y = param(g)\n"
        "float_ty[32] sub_one = fadd[](y, minus_one)\n";

    struct fir_mod* mod = fir_mod_create("module");
    REQUIRE(fir_mod_parse(mod, &(struct fir_parse_input) {
        .file_name = "stdin",
        .file_data = data,
        .file_size = strlen(data),
        .error_log = stderr
    }));
    struct fir_node** funcs = fir_mod_funcs(mod);
    REQUIRE(fir_mod_func_count(mod) == 2);

    enum fir_fp_flags fp_flags = FIR_FP_NO_SIGNED_ZERO | FIR_FP_ASSOCIATIVE;
    const struct fir_node* float32_ty = fir_float_ty(mod, 32);
    const struct fir_node* x = fir_param(funcs[0]);
    const struct fir_node* one = fir_float_const(float32_ty, 1.);
    const struct fir_node* add_one = fir_farith_op(FIR_FADD, fp_flags, x, one);
    REQUIRE(funcs[0]->ty == fir_func_ty(float32_ty, float32_ty));
    REQUIRE(funcs[0]->ops[0] == add_one);
    REQUIRE(funcs[0]->ops[0]->data.fp_flags == fp_flags);

    const struct fir_node* y = fir_param(funcs[1]);
    const struct fir_node* minus_one = fir_float_const(float32_ty, -1.);
    const struct fir_node* sub_one = fir_farith_op(FIR_FADD, FIR_FP_STRICT, y, minus_one);
    REQUIRE(funcs[1]->ty == fir_func_ty(float32_ty, float32_ty));
    REQUIRE(funcs[1]->ops[0] == sub_one);
    REQUIRE(funcs[1]->ops[0]->data.fp_flags == FIR_FP_STRICT);

    fir_mod_destroy(mod);
}
