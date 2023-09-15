#include "macros.h"

#include <fir/module.h>
#include <fir/node.h>

TEST(module) {
    struct fir_mod* mod = fir_mod_create("module");

    for (uint32_t i = 8; i <= 64; i *= 2)
        REQUIRE(fir_int_ty(mod, i) == fir_int_ty(mod, i));
    for (uint32_t i = 32; i <= 64; i *= 2)
        REQUIRE(fir_float_ty(mod, i) == fir_float_ty(mod, i));

    REQUIRE(fir_node_mod(fir_int_ty(mod, 32)) == mod);
    REQUIRE(fir_node_has_bitwidth(fir_int_ty(mod, 32)));
    const struct fir_node* forty_two = fir_int_const(fir_int_ty(mod, 32), 42);
    REQUIRE(forty_two == fir_int_const(fir_int_ty(mod, 32), 42));
    REQUIRE(forty_two != fir_int_const(fir_int_ty(mod, 32), 10));
    REQUIRE(fir_float_const(fir_float_ty(mod, 32), 42.f) == fir_float_const(fir_float_ty(mod, 32), 42.f));
    REQUIRE(fir_float_const(fir_float_ty(mod, 32), 0.f) != fir_float_const(fir_float_ty(mod, 32), -0.f));
    const struct fir_node* tup_args[] = { fir_int_ty(mod, 32), fir_int_ty(mod, 32) };
    REQUIRE(fir_tup_ty(mod, tup_args, 2) == fir_tup_ty(mod, tup_args, 2));
    REQUIRE(
        fir_func_ty(fir_int_ty(mod, 32), fir_int_ty(mod, 32)) ==
        fir_func_ty(fir_int_ty(mod, 32), fir_int_ty(mod, 32)));

    struct fir_node* func = fir_func(fir_func_ty(fir_int_ty(mod, 32), fir_int_ty(mod, 32)));
    for (size_t i = 0; i < 10; ++i)
        fir_node_set_op(func, 0, forty_two);
    REQUIRE(func->ops[0] == forty_two);
    REQUIRE(fir_mod_func_count(mod) == 1);

    fir_mod_destroy(mod);
}
