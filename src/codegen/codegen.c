#include "fir/codegen.h"
#include "fir/node.h"
#include "fir/module.h"

#include "codegen.h"

#include <assert.h>
#include <string.h>

struct fir_codegen* fir_codegen_create(
    enum fir_codegen_tag tag,
    [[maybe_unused]] const char** options,
    [[maybe_unused]] size_t option_count)
{
    switch (tag) {
#ifdef FIR_ENABLE_LLVM_CODEGEN
        case FIR_CODEGEN_LLVM:
            return llvm_codegen_create(options, option_count);
#endif
        case FIR_CODEGEN_DUMMY:
            return dummy_codegen_create();
        default:
            return NULL;
    }
}

void fir_codegen_destroy(struct fir_codegen* machine) {
    machine->destroy(machine);
}

bool fir_codegen_run(
    struct fir_codegen* codegen,
    struct fir_mod* mod,
    const char* output_file)
{
    return codegen->run(codegen, mod, output_file);
}
