add_library(libfir_codegen codegen.c)
target_include_directories(libfir_codegen PUBLIC ${PROJECT_SOURCE_DIR}/include)

add_subdirectory(dummy)
if (ENABLE_LLVM_CODEGEN)
    add_subdirectory(llvm)
endif()
