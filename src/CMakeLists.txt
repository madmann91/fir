add_library(libfir_support STATIC
    support/datatypes.c
    support/alloc.c
    support/primes.c
    support/scanner.c
    support/parser.c)

add_library(libfir_analysis STATIC
    analysis/scope.c
    analysis/cfg.c)

add_library(libfir
    dbg_info.c
    node.c
    module.c
    parse.c
    print.c)

add_executable(fir main.c)

target_include_directories(libfir PUBLIC ../include)
target_include_directories(libfir_support PUBLIC ../include)
target_include_directories(libfir_analysis PUBLIC ../include)

target_compile_options(libfir PRIVATE -DFIR_EXPORT_SYMBOLS)
target_link_libraries(libfir PRIVATE libfir_support libfir_analysis)

include(CheckLibraryExists)
check_library_exists(m sin "" LINK_LIBM)
if (LINK_LIBM)
    target_link_libraries(libfir PUBLIC m)
endif()

set_target_properties(libfir PROPERTIES
    PREFIX ""
    C_VISIBILITY_PRESET hidden)

set_target_properties(libfir_support libfir_analysis PROPERTIES
    PREFIX ""
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden)

target_link_libraries(fir PRIVATE libfir)
