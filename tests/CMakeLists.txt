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

add_executable(test_tic12400 test_tic12400.c ../Core/Src/tic12400.c)
target_link_libraries(test_tic12400 unity)

add_executable(
    test_tic12400_switch
    test_tic12400_switch.c
    ../Core/Src/tic12400_switch.c
)
target_link_libraries(test_tic12400_switch unity)

add_executable(
    test_tic12400_recovery
    test_tic12400_recovery.c
    ../Core/Src/tic12400_recovery.c
)
target_link_libraries(test_tic12400_recovery unity)

add_executable(
    test_tic12400_profile
    test_tic12400_profile.c
    ../Core/Src/tic12400_profile.c
)
target_link_libraries(test_tic12400_profile unity)

add_executable(
    test_app_control_policy
    test_app_control_policy.c
    ../Core/Src/app_control_policy.c
)
target_link_libraries(test_app_control_policy unity)

add_executable(
    test_app_control
    test_app_control.c
    ../Core/Src/app_control.c
    ../Core/Src/app_control_policy.c
)
target_link_libraries(test_app_control unity)

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

add_executable(test_app_fault test_app_fault.c ../Core/Src/app_fault.c)
target_compile_definitions(test_app_fault PRIVATE APP_FAULT_HOST_TEST)
target_link_libraries(test_app_fault unity)

add_executable(test_app_startup test_app_startup.c ../Core/Src/app_startup.c)
target_link_libraries(test_app_startup unity)

add_executable(
    test_can_rx_health
    test_can_rx_health.c
    ../Core/Src/can_rx_health.c
)
target_link_libraries(test_can_rx_health unity)

add_executable(test_app_timing test_app_timing.c ../Core/Src/app_timing.c)
target_compile_definitions(test_app_timing PRIVATE APP_TIMING_HOST_TEST)
target_link_libraries(test_app_timing unity)

# Register tests
enable_testing()
add_test(NAME test_can_protocol COMMAND test_can_protocol)
add_test(NAME test_pca2131_validation COMMAND test_pca2131_validation)
add_test(NAME test_tic12400 COMMAND test_tic12400)
add_test(NAME test_tic12400_switch COMMAND test_tic12400_switch)
add_test(NAME test_tic12400_recovery COMMAND test_tic12400_recovery)
add_test(NAME test_tic12400_profile COMMAND test_tic12400_profile)
add_test(NAME test_app_control_policy COMMAND test_app_control_policy)
add_test(NAME test_app_control COMMAND test_app_control)
add_test(NAME test_can_transport COMMAND test_can_transport)
add_test(NAME test_pwm_self_test COMMAND test_pwm_self_test)
add_test(NAME test_can_command_guard COMMAND test_can_command_guard)
add_test(NAME test_app_fault COMMAND test_app_fault)
add_test(NAME test_app_startup COMMAND test_app_startup)
add_test(NAME test_can_rx_health COMMAND test_can_rx_health)
add_test(NAME test_app_timing COMMAND test_app_timing)
