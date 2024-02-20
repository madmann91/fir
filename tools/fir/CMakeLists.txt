add_executable(fir main.c)
target_link_libraries(fir PRIVATE libfir libfir_support libfir_codegen)
