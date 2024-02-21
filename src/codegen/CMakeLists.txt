add_library(libfir_codegen codegen.c)
target_include_directories(libfir_codegen PUBLIC ${PROJECT_SOURCE_DIR}/include)
set_target_properties(libfir_codegen PROPERTIES PREFIX "")

add_subdirectory(dummy)
if (ENABLE_LLVM_CODEGEN)
    add_subdirectory(llvm)
endif()
