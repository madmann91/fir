add_executable(unit_tests
    main.c
    test.c
    unit/dbg_info.c
    unit/module.c
    unit/parse.c
    unit/support/mem_pool.c
    unit/support/map.c
    unit/support/set.c
    unit/support/heap.c
    unit/analysis/cfg.c)

target_include_directories(unit_tests PRIVATE ../src .)
target_link_libraries(unit_tests PRIVATE libfir libfir_support libfir_analysis)

if (WIN32 OR TEST_DISABLE_FORK)
    target_compile_definitions(unit_tests PRIVATE -DTEST_DISABLE_FORK)
else()
    message(STATUS "Building tests with process isolation enabled")
endif()

# PCRE2 does not come with a configuration file. This crude hack auto-detects its presence on most
# systems and compiles with it when it is available.
if (NOT TEST_DISABLE_REGEX)
    find_library(PCRE2_LIB pcre2-8)
    if (PCRE2_LIB)
        message(STATUS "Building tests with regular expression support")
        target_link_libraries(unit_tests PRIVATE ${PCRE2_LIB})
    else()
        message(WARNING "Cannot find PCRE2, disabling regular expression support in tests")
        target_compile_definitions(unit_tests PRIVATE TEST_DISABLE_REGEX)
    endif()
else()
    target_compile_definitions(unit_tests PRIVATE TEST_DISABLE_REGEX)
endif ()

add_test(NAME unit_tests COMMAND unit_tests)
add_custom_target(memcheck COMMAND ${CMAKE_CTEST_COMMAND} -T memcheck WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

if (ENABLE_COVERAGE)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/coverage.html
        DEPENDS libfir libfir_support libfir_analysis unit_tests memcheck
        COMMAND ${GCOVR_COMMAND} -r ${PROJECT_SOURCE_DIR} --html coverage.html
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    add_custom_target(coverage
        DEPENDS ${CMAKE_BINARY_DIR}/coverage.html)
endif()
