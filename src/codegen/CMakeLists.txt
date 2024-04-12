add_library(libfir_codegen INTERFACE)

add_subdirectory(dummy)
if (ENABLE_LLVM_CODEGEN)
    add_subdirectory(llvm)
endif()
