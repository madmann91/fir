add_library(libfir_support STATIC
    support/alloc.c
    support/primes.c
    support/str.c
    support/htable.c
    support/scanner.c
    support/parser.c)

add_library(libfir
    dbg_info.c
    node.c
    module.c
    parse.c
    print.c)

target_include_directories(libfir PUBLIC ../include)
target_include_directories(libfir_support PUBLIC ../include)

target_compile_options(libfir PRIVATE -DFIR_EXPORT_SYMBOLS)
target_link_libraries(libfir PRIVATE libfir_support)
set_target_properties(libfir PROPERTIES
    PREFIX ""
    C_VISIBILITY_PRESET hidden)

set_target_properties(libfir_support PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden)
