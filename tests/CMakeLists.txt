add_executable(cpu_unit_test cpu_unit_test.cpp)
target_link_libraries(cpu_unit_test PRIVATE nes_core)
add_test(NAME cpu_unit_test COMMAND cpu_unit_test)

add_executable(cpu_nestest_test cpu_nestest_test.cpp)
target_link_libraries(cpu_nestest_test PRIVATE nes_core)
target_compile_definitions(cpu_nestest_test PRIVATE TESTS_ROMS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/roms")
add_test(NAME cpu_nestest_test COMMAND cpu_nestest_test)

add_executable(mapper_unit_test mapper_unit_test.cpp)
target_link_libraries(mapper_unit_test PRIVATE nes_core)
add_test(NAME mapper_unit_test COMMAND mapper_unit_test)

add_executable(apu_unit_test apu_unit_test.cpp)
target_link_libraries(apu_unit_test PRIVATE nes_core)
add_test(NAME apu_unit_test COMMAND apu_unit_test)

add_executable(save_state_unit_test save_state_unit_test.cpp)
target_link_libraries(save_state_unit_test PRIVATE nes_core)
add_test(NAME save_state_unit_test COMMAND save_state_unit_test)
