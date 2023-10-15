add_executable(ash main.c ast.c lexer.c token.c parser.c types.c bind.c check.c emit.c)
target_link_libraries(ash PRIVATE libfir libfir_support)
