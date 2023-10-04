add_executable(ash main.c ast.c lexer.c token.c)
target_link_libraries(ash PRIVATE libfir libfir_support)
