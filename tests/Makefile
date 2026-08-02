CC = gcc
PYTHON ?= python3
WARNING_FLAGS = -Wall -Wextra -Wconversion -Wshadow -Wundef -Werror
CFLAGS = $(WARNING_FLAGS) -Iunity -Istubs -I../Core/Inc -g -O0
BUILD_DIR = build
PYTHON_TESTS = $(wildcard ../python/tests/test_gui_*.py)

TEST_PROTOCOL_SRC = test_can_protocol.c ../Core/Src/can_protocol.c unity/unity.c
TEST_PCA2131_SRC = test_pca2131_validation.c ../Core/Src/pca2131.c stubs/pca2131_hal_stubs.c unity/unity.c
TEST_RTC_APP_SRC = test_rtc_app.c ../Core/Src/rtc_app.c \
	../Core/Src/can_protocol.c unity/unity.c
TEST_TIC12400_SRC = test_tic12400.c ../Core/Src/tic12400.c unity/unity.c
TEST_TIC12400_SWITCH_SRC = test_tic12400_switch.c \
	../Core/Src/tic12400_switch.c unity/unity.c
TEST_TIC12400_RECOVERY_SRC = test_tic12400_recovery.c \
	../Core/Src/tic12400_recovery.c unity/unity.c
TEST_TIC12400_PROFILE_SRC = test_tic12400_profile.c \
	../Core/Src/tic12400_profile.c unity/unity.c
TEST_TIC12400_PROBE_SRC = test_tic12400_probe.c \
	../Core/Src/tic12400_probe.c ../Core/Src/tic12400_profile.c \
	../Core/Src/tic12400_recovery.c ../Core/Src/tic12400_switch.c unity/unity.c
TEST_APP_CONTROL_POLICY_SRC = test_app_control_policy.c \
	../Core/Src/app_control_policy.c unity/unity.c
TEST_APP_CONTROL_SRC = test_app_control.c \
	../Core/Src/app_control.c ../Core/Src/app_control_policy.c unity/unity.c
TEST_TRANSPORT_SRC = test_can_transport.c ../Core/Src/can_transport.c unity/unity.c
TEST_PWM_SELF_TEST_SRC = test_pwm_self_test.c ../Core/Src/pwm_self_test.c unity/unity.c
TEST_COMMAND_GUARD_SRC = test_can_command_guard.c \
	../Core/Src/can_command_guard.c ../Core/Src/can_protocol.c unity/unity.c
TEST_APP_LOG_SRC = test_app_log.c ../Core/Src/app_log.c unity/unity.c
TEST_APP_FAULT_SRC = test_app_fault.c ../Core/Src/app_fault.c unity/unity.c
TEST_APP_STARTUP_SRC = test_app_startup.c ../Core/Src/app_startup.c unity/unity.c
TEST_CAN_RX_HEALTH_SRC = test_can_rx_health.c \
	../Core/Src/can_rx_health.c unity/unity.c
TEST_APP_TIMING_SRC = test_app_timing.c ../Core/Src/app_timing.c unity/unity.c
TEST_APP_IRQ_POLICY_SRC = test_app_irq_policy.c \
	../Core/Src/app_irq_policy.c unity/unity.c
TEST_CAN_RECOVERY_SRC = test_can_recovery.c \
	../Core/Src/can_recovery.c unity/unity.c
TEST_CAN_APP_INIT_SRC = test_can_app_init.c \
	../Core/Src/can_app_init.c unity/unity.c
TEST_CAN_CONTROL_ACCESS_SRC = test_can_control_access.c \
	../Core/Src/can_control_access.c unity/unity.c
TEST_CAN_COMMAND_VALIDATION_SRC = test_can_command_validation.c \
	../Core/Src/can_command_validation.c ../Core/Src/can_protocol.c \
	../Core/Src/pca2131.c stubs/pca2131_hal_stubs.c unity/unity.c
TEST_CAN_APP_RX_SRC = test_can_app_rx.c ../Core/Src/can_app.c \
	../Core/Src/can_command_validation.c ../Core/Src/can_command_guard.c \
	../Core/Src/can_control_access.c ../Core/Src/can_protocol.c unity/unity.c

TEST_BINS = \
	$(BUILD_DIR)/test_protocol \
	$(BUILD_DIR)/test_pca2131 \
	$(BUILD_DIR)/test_rtc_app \
	$(BUILD_DIR)/test_tic12400 \
	$(BUILD_DIR)/test_tic12400_switch \
	$(BUILD_DIR)/test_tic12400_recovery \
	$(BUILD_DIR)/test_tic12400_profile \
	$(BUILD_DIR)/test_tic12400_probe \
	$(BUILD_DIR)/test_app_control_policy \
	$(BUILD_DIR)/test_app_control \
	$(BUILD_DIR)/test_transport \
	$(BUILD_DIR)/test_pwm_self_test \
	$(BUILD_DIR)/test_command_guard \
	$(BUILD_DIR)/test_app_log \
	$(BUILD_DIR)/test_app_fault \
	$(BUILD_DIR)/test_app_startup \
	$(BUILD_DIR)/test_can_rx_health \
	$(BUILD_DIR)/test_app_timing \
	$(BUILD_DIR)/test_app_irq_policy \
	$(BUILD_DIR)/test_can_recovery \
	$(BUILD_DIR)/test_can_app_init \
	$(BUILD_DIR)/test_can_control_access \
	$(BUILD_DIR)/test_can_command_validation \
	$(BUILD_DIR)/test_can_app_rx

.PHONY: all test test-python test-stack-usage clean

all: $(TEST_BINS)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/test_protocol: $(TEST_PROTOCOL_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_pca2131: $(TEST_PCA2131_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_rtc_app: $(TEST_RTC_APP_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -D__MAIN_H -include stubs/main.h \
		-include ../Core/Inc/rtc_app.h $^ -o $@

$(BUILD_DIR)/test_tic12400: $(TEST_TIC12400_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_tic12400_switch: $(TEST_TIC12400_SWITCH_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_tic12400_recovery: $(TEST_TIC12400_RECOVERY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_tic12400_profile: $(TEST_TIC12400_PROFILE_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_tic12400_probe: $(TEST_TIC12400_PROBE_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_app_control_policy: $(TEST_APP_CONTROL_POLICY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_app_control: $(TEST_APP_CONTROL_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_transport: $(TEST_TRANSPORT_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_pwm_self_test: $(TEST_PWM_SELF_TEST_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_command_guard: $(TEST_COMMAND_GUARD_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_app_log: $(TEST_APP_LOG_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DAPP_LOG_HOST_TEST $^ -o $@

$(BUILD_DIR)/test_app_fault: $(TEST_APP_FAULT_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DAPP_FAULT_HOST_TEST $^ -o $@

$(BUILD_DIR)/test_app_startup: $(TEST_APP_STARTUP_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_can_rx_health: $(TEST_CAN_RX_HEALTH_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_app_timing: $(TEST_APP_TIMING_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DAPP_TIMING_HOST_TEST $^ -o $@

$(BUILD_DIR)/test_app_irq_policy: $(TEST_APP_IRQ_POLICY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_can_recovery: $(TEST_CAN_RECOVERY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_can_app_init: $(TEST_CAN_APP_INIT_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_can_control_access: $(TEST_CAN_CONTROL_ACCESS_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_can_command_validation: $(TEST_CAN_COMMAND_VALIDATION_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_can_app_rx: $(TEST_CAN_APP_RX_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $^ \
		-Wl,--gc-sections -o $@

test: all
	$(BUILD_DIR)/test_protocol
	$(BUILD_DIR)/test_pca2131
	$(BUILD_DIR)/test_rtc_app
	$(BUILD_DIR)/test_tic12400
	$(BUILD_DIR)/test_tic12400_switch
	$(BUILD_DIR)/test_tic12400_recovery
	$(BUILD_DIR)/test_tic12400_profile
	$(BUILD_DIR)/test_tic12400_probe
	$(BUILD_DIR)/test_app_control_policy
	$(BUILD_DIR)/test_app_control
	$(BUILD_DIR)/test_transport
	$(BUILD_DIR)/test_pwm_self_test
	$(BUILD_DIR)/test_command_guard
	$(BUILD_DIR)/test_app_log
	$(BUILD_DIR)/test_app_fault
	$(BUILD_DIR)/test_app_startup
	$(BUILD_DIR)/test_can_rx_health
	$(BUILD_DIR)/test_app_timing
	$(BUILD_DIR)/test_app_irq_policy
	$(BUILD_DIR)/test_can_recovery
	$(BUILD_DIR)/test_can_app_init
	$(BUILD_DIR)/test_can_control_access
	$(BUILD_DIR)/test_can_command_validation
	$(BUILD_DIR)/test_can_app_rx

test-python:
	@$(PYTHON) -m compileall -q \
		../python/can_gui.py ../python/can_gui_app ../python/tests
	@for test_file in $(PYTHON_TESTS); do \
		PYTHONDONTWRITEBYTECODE=1 $(PYTHON) $$test_file || exit 1; \
	done

test-stack-usage:
	@PYTHONDONTWRITEBYTECODE=1 $(PYTHON) host/test_stack_usage_report.py

clean:
	rm -f $(TEST_BINS)
	rmdir $(BUILD_DIR) 2>/dev/null || true
