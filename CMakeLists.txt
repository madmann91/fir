cmake_minimum_required(VERSION 3.20)
project(fir VERSION 0.0.1)

option(BUILD_SHARED_LIBS "Build a shared library." ON)
option(BUILD_TOOLS "Build tools." ON)
option(TEST_DISABLE_FORK "Disable fork() in the testing framework." OFF)
option(TEST_DISABLE_REGEX "Disable regular expressions when filtering tests." OFF)
option(ENABLE_COVERAGE "Turn on code coverage in debug mode." OFF)

add_compile_options(
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall>
    $<$<C_COMPILER_ID:GNU,Clang>:-Wextra>
    $<$<C_COMPILER_ID:GNU,Clang>:-pedantic>)

set(CMAKE_C_STANDARD 23)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) 
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) 
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_library(coverage_options INTERFACE)
if (ENABLE_COVERAGE)
    message(STATUS "Enabling code coverage analysis in debug mode")
    find_program(GCOVR_COMMAND gcovr REQUIRED)
    if (GCOVR_COMMAND)
        set(coverage_option "$<$<AND:$<CONFIG:Debug>,$<C_COMPILER_ID:GNU,Clang>>:--coverage>")
        target_compile_options(coverage_options INTERFACE ${coverage_option})
        target_link_options(coverage_options INTERFACE ${coverage_option})
    endif()
endif()

add_subdirectory(src)
if (BUILD_TOOLS)
    add_subdirectory(tools)
endif()

find_package(Doxygen QUIET)
if (DOXYGEN_FOUND)
    message(STATUS "Enabling Doxygen documentation target")
    add_custom_target(doc COMMAND Doxygen::doxygen WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/doc)
endif()

include(CTest)
if (BUILD_TESTING)
    add_subdirectory(test)
endif()
