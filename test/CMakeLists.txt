add_executable(unit_tests
    main.c
    unit/dbg_info.c
    unit/module.c
    unit/parse.c
    unit/support/map.c
    unit/support/set.c)

target_include_directories(unit_tests PRIVATE ../src .)
target_link_libraries(unit_tests PRIVATE libfir libfir_support)

# PCRE2 does not come with a configuration file. This crude hack auto-detects its presence on most
# systems and compiles with it when it is available.
find_library(PCRE2_LIB pcre2-8)
if (PCRE2_LIB)
    target_compile_definitions(unit_tests PRIVATE ENABLE_REGEX)
    target_link_libraries(unit_tests PRIVATE ${PCRE2_LIB})
endif ()

add_test(NAME unit_tests COMMAND unit_tests)
add_custom_target(memcheck COMMAND ${CMAKE_CTEST_COMMAND} -T memcheck WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
