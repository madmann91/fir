#pragma once

#include <stddef.h>
#include <stdbool.h>

struct fir_mod;
struct fir_node;
struct graph_node;

struct fir_codegen {
    void (*destroy)(struct fir_codegen*);
    bool (*run)(struct fir_codegen*, struct fir_mod*, const char*);
};

struct fir_codegen* dummy_codegen_create(void);
#ifdef FIR_ENABLE_LLVM_CODEGEN
struct fir_codegen* llvm_codegen_create(const char**, size_t);
#endif
