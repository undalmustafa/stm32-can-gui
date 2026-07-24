cmake_minimum_required(VERSION 3.10)
project(can_gui_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# Include directories
include_directories(
    unity
    stubs
    ../Core/Inc
)

# Unity library
add_library(unity STATIC unity/unity.c)

# Add tests
add_executable(test_can_protocol test_can_protocol.c ../Core/Src/can_protocol.c)
target_link_libraries(test_can_protocol unity)

add_executable(
    test_pca2131_validation
    test_pca2131_validation.c
    ../Core/Src/pca2131.c
    stubs/pca2131_hal_stubs.c
)
target_link_libraries(test_pca2131_validation unity)

add_executable(test_can_transport test_can_transport.c ../Core/Src/can_transport.c)
target_link_libraries(test_can_transport unity)

add_executable(test_pwm_self_test test_pwm_self_test.c ../Core/Src/pwm_self_test.c)
target_link_libraries(test_pwm_self_test unity)

add_executable(
    test_can_command_guard
    test_can_command_guard.c
    ../Core/Src/can_command_guard.c
    ../Core/Src/can_protocol.c
)
target_link_libraries(test_can_command_guard unity)

# Register tests
enable_testing()
add_test(NAME test_can_protocol COMMAND test_can_protocol)
add_test(NAME test_pca2131_validation COMMAND test_pca2131_validation)
add_test(NAME test_can_transport COMMAND test_can_transport)
add_test(NAME test_pwm_self_test COMMAND test_pwm_self_test)
add_test(NAME test_can_command_guard COMMAND test_can_command_guard)
