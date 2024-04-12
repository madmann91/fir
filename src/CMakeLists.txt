add_library(libfir_support STATIC
    datatypes.c)

add_library(libfir_analysis STATIC
    analysis/liveness.c
    analysis/schedule.c
    analysis/loop_tree.c
    analysis/dom_tree.c
    analysis/scope.c
    analysis/cfg.c)

add_library(libfir
    version.c
    dbg_info.c
    block.c
    node.c
    module.c
    print.c
    parse/parse.c
    parse/lexer.c
    parse/token.c
    codegen/codegen.c)

add_subdirectory(codegen)

target_include_directories(libfir_support PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(libfir_support PUBLIC
    overture
    overture_str_pool
    overture_mem_pool
    overture_graph
    overture_log)

target_include_directories(libfir PUBLIC
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

string(TIMESTAMP timestamp "%Y%m%d")
target_compile_options(libfir PRIVATE
    -DFIR_EXPORT_SYMBOLS
    -DFIR_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    -DFIR_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    -DFIR_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    -DFIR_VERSION_TIMESTAMP=${timestamp})

target_link_libraries(libfir_analysis  PUBLIC libfir_support)
target_link_libraries(libfir PRIVATE libfir_codegen libfir_analysis)

include(CheckLibraryExists)
check_library_exists(m sin "" LINK_LIBM)
if (LINK_LIBM)
    target_link_libraries(libfir PUBLIC m)
endif()

set_target_properties(libfir_support libfir_analysis PROPERTIES
    POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS})
set_target_properties(libfir libfir_analysis libfir_support PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
