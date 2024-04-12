#include "codegen/codegen.h"

#include <overture/mem.h>

#include <stdlib.h>

static bool dummy_codegen_run(struct fir_codegen*, struct fir_mod*, const char*) {
    return true;
}

static void dummy_codegen_destroy(struct fir_codegen* codegen) {
    free(codegen);
}

struct fir_codegen* dummy_codegen_create(void) {
    struct fir_codegen* codegen = xmalloc(sizeof(struct fir_codegen));
    codegen->destroy = dummy_codegen_destroy;
    codegen->run = dummy_codegen_run;
    return codegen;
}
