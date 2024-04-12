add_executable(unit_tests
    main.c
    dbg_info.c
    module.c
    parse.c
    analysis/cfg.c)

target_include_directories(unit_tests PRIVATE ../src)
target_link_libraries(unit_tests PRIVATE libfir libfir_analysis overture_test)

add_test(NAME unit_tests COMMAND unit_tests)
add_custom_target(memcheck COMMAND ${CMAKE_CTEST_COMMAND} -T memcheck WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_dependencies(memcheck unit_tests)
add_dependencies(coverage memcheck)
