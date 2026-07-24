CC = gcc
CFLAGS = -Wall -Wextra -Iunity -Istubs -I../Core/Inc -g -O0

TEST_PROTOCOL_SRC = test_can_protocol.c ../Core/Src/can_protocol.c unity/unity.c
TEST_PCA2131_SRC = test_pca2131_validation.c ../Core/Src/pca2131.c unity/unity.c
TEST_TRANSPORT_SRC = test_can_transport.c ../Core/Src/can_transport.c unity/unity.c
TEST_PWM_SELF_TEST_SRC = test_pwm_self_test.c ../Core/Src/pwm_self_test.c unity/unity.c

all: test_protocol test_pca2131 test_transport test_pwm_self_test

test_protocol: $(TEST_PROTOCOL_SRC)
	$(CC) $(CFLAGS) $^ -o $@

test_pca2131: $(TEST_PCA2131_SRC)
	$(CC) $(CFLAGS) $^ -o $@

test_transport: $(TEST_TRANSPORT_SRC)
	$(CC) $(CFLAGS) $^ -o $@

test_pwm_self_test: $(TEST_PWM_SELF_TEST_SRC)
	$(CC) $(CFLAGS) $^ -o $@

test: all
	./test_protocol
	./test_pca2131
	./test_transport
	./test_pwm_self_test

clean:
	rm -f test_protocol test_pca2131 test_transport test_pwm_self_test
