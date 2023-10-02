add_library(libfir_support STATIC
    support/io.c
    support/mem_pool.c
    support/datatypes.c
    support/alloc.c
    support/primes.c)

add_library(libfir_analysis STATIC
    analysis/scope.c
    analysis/cfg.c)

add_library(libfir
    version.c
    dbg_info.c
    node.c
    module.c
    parse/parse.c
    parse/lexer.c
    parse/token.c
    print.c)

target_include_directories(libfir PUBLIC ../include)
target_include_directories(libfir_support PUBLIC ../include ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(libfir_analysis PUBLIC libfir_support)

string(TIMESTAMP timestamp "%Y%m%d")

target_compile_options(libfir PRIVATE
    -DFIR_EXPORT_SYMBOLS
    -DFIR_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    -DFIR_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    -DFIR_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    -DFIR_VERSION_TIMESTAMP=${timestamp})
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
