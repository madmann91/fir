#ifndef FIR_CODEGEN_H
#define FIR_CODEGEN_H

#include "fir/platform.h"

#include <stddef.h>
#include <stdbool.h>

/// @file
///
/// Code generation is the last step to produce machine code. This is typically run after every
/// optimization pass has been run.

/// Enumeration containing the types of code generators.
enum fir_codegen_tag {
    FIR_CODEGEN_DUMMY, ///< Dummy backend that does nothing.
    FIR_CODEGEN_LLVM   ///< Code-generation through LLVM.
};

struct fir_mod;

/// Abstract datatype representing a target machine to generate code for.
struct fir_codegen;

/// Creates a code generator from the given options specified as character strings. Available
/// options depend on the code generator type. If the given code generator type is not available or
/// cannot be created with the given set of options, this function returns `NULL`.
FIR_SYMBOL struct fir_codegen* fir_codegen_create(
    enum fir_codegen_tag,
    const char** options,
    size_t option_count);

/// Destroys the given code generator.
FIR_SYMBOL void fir_codegen_destroy(struct fir_codegen*);

/// Generates code for the given IR module and code generator object. The module may be modified by
/// the code generator in order to simplify code generation, or to lower some unsupported constructs
/// into supported ones. Returns `true` on success, `false` on failure.
FIR_SYMBOL bool fir_codegen_run(
    struct fir_codegen*,
    struct fir_mod*,
    const char* output_file);

#endif
