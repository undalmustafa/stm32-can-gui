CC = gcc
CFLAGS = -Wall -Wextra -Iunity -Istubs -I../Core/Inc -g -O0
BUILD_DIR = build

TEST_PROTOCOL_SRC = test_can_protocol.c ../Core/Src/can_protocol.c unity/unity.c
TEST_PCA2131_SRC = test_pca2131_validation.c ../Core/Src/pca2131.c stubs/pca2131_hal_stubs.c unity/unity.c
TEST_TRANSPORT_SRC = test_can_transport.c ../Core/Src/can_transport.c unity/unity.c
TEST_PWM_SELF_TEST_SRC = test_pwm_self_test.c ../Core/Src/pwm_self_test.c unity/unity.c

TEST_BINS = \
	$(BUILD_DIR)/test_protocol \
	$(BUILD_DIR)/test_pca2131 \
	$(BUILD_DIR)/test_transport \
	$(BUILD_DIR)/test_pwm_self_test

.PHONY: all test clean

all: $(TEST_BINS)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/test_protocol: $(TEST_PROTOCOL_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_pca2131: $(TEST_PCA2131_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_transport: $(TEST_TRANSPORT_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_pwm_self_test: $(TEST_PWM_SELF_TEST_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

test: all
	$(BUILD_DIR)/test_protocol
	$(BUILD_DIR)/test_pca2131
	$(BUILD_DIR)/test_transport
	$(BUILD_DIR)/test_pwm_self_test

clean:
	rm -f $(TEST_BINS)
	rmdir $(BUILD_DIR) 2>/dev/null || true
