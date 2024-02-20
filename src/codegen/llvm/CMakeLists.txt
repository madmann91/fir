message(STATUS "Enabling LLVM code generation") 

find_package(LLVM CONFIG REQUIRED)
llvm_map_components_to_libnames(llvm_libs support core bitreader x86codegen)

add_library(libfir_codegen_llvm OBJECT llvm.c)

target_compile_options(libfir_codegen_llvm PUBLIC -DFIR_ENABLE_LLVM_CODEGEN)
set_target_properties(libfir_codegen_llvm PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(libfir_codegen_llvm PUBLIC libfir_support libfir_analysis)
target_link_libraries(libfir_codegen_llvm PRIVATE ${llvm_libs})
target_link_libraries(libfir_codegen PRIVATE libfir_codegen_llvm)
