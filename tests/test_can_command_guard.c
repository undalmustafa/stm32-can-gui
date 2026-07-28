#include "unity.h"

#include "can_command_guard.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_safe_commands_do_not_require_service_access(void)
{
    const uint8_t pwm_stop[8] =
        {CAN_PROTOCOL_CMD_PWM_SET, 0U, 0U, 0U, 0U, 90U, 0U, 0U};
    const uint8_t log_info[8] =
        {CAN_PROTOCOL_CMD_LOG_GET_INFO, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t led_off[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 1U, 0U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t slot_disable[8] =
        {CAN_PROTOCOL_CMD_SET_SLOT_1, 0U, 0x23U, 0x01U, 0U, 0U, 10U, 0U};
    const uint8_t counter_stop[8] =
        {CAN_PROTOCOL_CMD_START_SLOT_1_COUNTER, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t self_test_cancel[8] =
        {CAN_PROTOCOL_CMD_PWM_SELF_TEST, 0U, 0U, 0U, 0U, 0U, 0U, 0U};

    TEST_ASSERT_EQUAL_UINT8(
        0U, CAN_CommandGuard_IsPrivileged(pwm_stop));
    TEST_ASSERT_EQUAL_UINT8(
        0U, CAN_CommandGuard_IsPrivileged(log_info));
    TEST_ASSERT_EQUAL_UINT8(
        0U, CAN_CommandGuard_IsPrivileged(led_off));
    TEST_ASSERT_EQUAL_UINT8(
        0U, CAN_CommandGuard_IsPrivileged(slot_disable));
    TEST_ASSERT_EQUAL_UINT8(
        0U, CAN_CommandGuard_IsPrivileged(counter_stop));
    TEST_ASSERT_EQUAL_UINT8(
        0U, CAN_CommandGuard_IsPrivileged(self_test_cancel));
}

static void test_state_enabling_commands_require_service_access(void)
{
    const uint8_t pwm_start[8] =
        {CAN_PROTOCOL_CMD_PWM_SET, 0x10U, 0x27U, 0U, 0U, 90U, 0U, 0U};
    const uint8_t led_on[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 1U, 1U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t slot_enable[8] =
        {CAN_PROTOCOL_CMD_SET_SLOT_1,
         CAN_PROTOCOL_SLOT_FLAG_ENABLE,
         0x23U, 0x01U, 0U, 0U, 10U, 0U};
    const uint8_t counter_start[8] =
        {CAN_PROTOCOL_CMD_START_SLOT_1_COUNTER,
         0U, 1U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t self_test_start[8] =
        {CAN_PROTOCOL_CMD_PWM_SELF_TEST, 1U, 0U, 0U, 0U, 0U, 0U, 0U};

    TEST_ASSERT_EQUAL_UINT8(
        1U, CAN_CommandGuard_IsPrivileged(pwm_start));
    TEST_ASSERT_EQUAL_UINT8(
        1U, CAN_CommandGuard_IsPrivileged(led_on));
    TEST_ASSERT_EQUAL_UINT8(
        1U, CAN_CommandGuard_IsPrivileged(slot_enable));
    TEST_ASSERT_EQUAL_UINT8(
        1U, CAN_CommandGuard_IsPrivileged(counter_start));
    TEST_ASSERT_EQUAL_UINT8(
        1U, CAN_CommandGuard_IsPrivileged(self_test_start));
}

static void test_unknown_and_null_inputs_are_conservative(void)
{
    const uint8_t unknown[8] =
        {0xFFU, 0U, 0U, 0U, 0U, 0U, 0U, 0U};

    TEST_ASSERT_EQUAL_UINT8(
        1U, CAN_CommandGuard_IsPrivileged(unknown));
    TEST_ASSERT_EQUAL_UINT8(
        0U, CAN_CommandGuard_IsPrivileged(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_safe_commands_do_not_require_service_access);
    RUN_TEST(test_state_enabling_commands_require_service_access);
    RUN_TEST(test_unknown_and_null_inputs_are_conservative);
    return UNITY_END();
}
