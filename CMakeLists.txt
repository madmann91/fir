cmake_minimum_required(VERSION 3.20)
project(fir VERSION 0.0.1)

option(BUILD_SHARED_LIBS "Build a shared library." ON)
option(BUILD_TOOLS "Build tools." ON)
option(ENABLE_COVERAGE "Turn on code coverage build type and target." OFF)
option(ENABLE_DOXYGEN "Turn on code documentation target via Doxygen." ON)
option(ENABLE_LLVM_CODEGEN "Enables code generation via LLVM." ON)

add_compile_options(
    $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug,Coverage>>:-fsanitize=address>
    $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug,Coverage>>:-fsanitize=undefined>
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall>
    $<$<C_COMPILER_ID:GNU,Clang>:-Wextra>
    $<$<C_COMPILER_ID:GNU,Clang>:-pedantic>)

add_link_options(
    $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug,Coverage>>:-fsanitize=address>
    $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug,Coverage>>:-fsanitize=undefined>)

set(CMAKE_C_STANDARD 23)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include(contrib/overture/cmake/Doxygen.cmake)
include(contrib/overture/cmake/Coverage.cmake)

add_subdirectory(contrib)
add_subdirectory(src)
if (BUILD_TOOLS)
    add_subdirectory(tools)
endif()

include(CTest)
if (BUILD_TESTING)
    add_subdirectory(test)
endif()
