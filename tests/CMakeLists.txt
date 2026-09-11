add_executable(cpu_unit_test cpu_unit_test.cpp)
target_link_libraries(cpu_unit_test PRIVATE nes_core)
add_test(NAME cpu_unit_test COMMAND cpu_unit_test)

add_executable(cpu_nestest_test cpu_nestest_test.cpp)
target_link_libraries(cpu_nestest_test PRIVATE nes_core)
target_compile_definitions(cpu_nestest_test PRIVATE TESTS_ROMS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/roms")
add_test(NAME cpu_nestest_test COMMAND cpu_nestest_test)
