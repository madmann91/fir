add_executable(ash main.c ast.c)
target_link_libraries(ash PRIVATE libfir libfir_support)
