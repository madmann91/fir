add_library(libfir_codegen_dummy OBJECT dummy.c)
set_target_properties(libfir_codegen_dummy PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(libfir_codegen_dummy PUBLIC libfir_support)
target_link_libraries(libfir_codegen PRIVATE libfir_codegen_dummy)
