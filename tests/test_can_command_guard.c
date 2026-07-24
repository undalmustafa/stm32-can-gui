#include "unity.h"

#include "can_command_guard.h"

static CAN_CommandGuard_t guard;

void setUp(void)
{
    CAN_CommandGuard_Init(&guard);
}

void tearDown(void)
{
}

static void test_command_requires_session(void)
{
    const uint8_t payload[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 1U, 1U, 0U, 0U, 0U, 0U, 0U};

    TEST_ASSERT_EQUAL(
        CAN_COMMAND_GUARD_SESSION_REQUIRED,
        CAN_CommandGuard_Check(&guard, 0xA5U, 1U, payload));
}

static void test_new_session_accepts_first_sequence(void)
{
    const uint8_t payload[8] =
        {CAN_PROTOCOL_CMD_SESSION_START, 1U, 2U, 3U, 4U, 1U, 0U, 0U};

    TEST_ASSERT_EQUAL(
        CAN_COMMAND_GUARD_ACCEPT,
        CAN_CommandGuard_StartSession(
            &guard, 0x04030201UL, 0xA5U, 27U, payload));
}

static void test_duplicate_is_not_accepted_twice(void)
{
    const uint8_t start[8] =
        {CAN_PROTOCOL_CMD_SESSION_START, 1U, 0U, 0U, 0U, 1U, 0U, 0U};
    const uint8_t command[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 1U, 1U, 0U, 0U, 0U, 0U, 0U};

    (void)CAN_CommandGuard_StartSession(
        &guard, 1U, 0xA5U, 10U, start);
    TEST_ASSERT_EQUAL(CAN_COMMAND_GUARD_ACCEPT,
                      CAN_CommandGuard_Check(
                          &guard, 0xA5U, 11U, command));
    TEST_ASSERT_EQUAL(CAN_COMMAND_GUARD_DUPLICATE,
                      CAN_CommandGuard_Check(
                          &guard, 0xA5U, 11U, command));
}

static void test_evaluation_does_not_consume_sequence(void)
{
    const uint8_t start[8] =
        {CAN_PROTOCOL_CMD_SESSION_START, 1U, 0U, 0U, 0U, 1U, 0U, 0U};
    const uint8_t command[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 1U, 1U, 0U, 0U, 0U, 0U, 0U};

    (void)CAN_CommandGuard_StartSession(
        &guard, 1U, 0xA5U, 10U, start);

    TEST_ASSERT_EQUAL(CAN_COMMAND_GUARD_ACCEPT,
                      CAN_CommandGuard_Evaluate(
                          &guard, 0xA5U, 11U, command));
    TEST_ASSERT_EQUAL(CAN_COMMAND_GUARD_ACCEPT,
                      CAN_CommandGuard_Evaluate(
                          &guard, 0xA5U, 11U, command));
    TEST_ASSERT_EQUAL(CAN_COMMAND_GUARD_ACCEPT,
                      CAN_CommandGuard_Check(
                          &guard, 0xA5U, 11U, command));
    TEST_ASSERT_EQUAL(CAN_COMMAND_GUARD_DUPLICATE,
                      CAN_CommandGuard_Evaluate(
                          &guard, 0xA5U, 11U, command));
}

static void test_same_sequence_with_changed_payload_is_replay(void)
{
    const uint8_t start[8] =
        {CAN_PROTOCOL_CMD_SESSION_START, 1U, 0U, 0U, 0U, 1U, 0U, 0U};
    const uint8_t first[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 1U, 1U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t changed[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 1U, 0U, 0U, 0U, 0U, 0U, 0U};

    (void)CAN_CommandGuard_StartSession(
        &guard, 1U, 0xA5U, 40U, start);
    (void)CAN_CommandGuard_Check(&guard, 0xA5U, 41U, first);

    TEST_ASSERT_EQUAL(CAN_COMMAND_GUARD_REPLAY,
                      CAN_CommandGuard_Check(
                          &guard, 0xA5U, 41U, changed));
}

static void test_delayed_retry_matches_recent_history(void)
{
    const uint8_t start[8] =
        {CAN_PROTOCOL_CMD_SESSION_START, 1U, 0U, 0U, 0U, 1U, 0U, 0U};
    const uint8_t first[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 1U, 1U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t second[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 2U, 1U, 0U, 0U, 0U, 0U, 0U};

    (void)CAN_CommandGuard_StartSession(
        &guard, 1U, 0xA5U, 10U, start);
    (void)CAN_CommandGuard_Check(&guard, 0xA5U, 11U, first);
    (void)CAN_CommandGuard_Check(&guard, 0xA5U, 12U, second);

    TEST_ASSERT_EQUAL(CAN_COMMAND_GUARD_DUPLICATE,
                      CAN_CommandGuard_Check(
                          &guard, 0xA5U, 11U, first));
}

static void test_old_sequence_is_rejected_after_wrap(void)
{
    const uint8_t start[8] =
        {CAN_PROTOCOL_CMD_SESSION_START, 1U, 0U, 0U, 0U, 1U, 0U, 0U};
    const uint8_t command[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 1U, 1U, 0U, 0U, 0U, 0U, 0U};

    (void)CAN_CommandGuard_StartSession(
        &guard, 1U, 0xA5U, 2U, start);

    TEST_ASSERT_EQUAL(CAN_COMMAND_GUARD_REPLAY,
                      CAN_CommandGuard_Check(
                          &guard, 0xA5U, 250U, command));
}

static void test_command_from_other_session_tag_is_rejected(void)
{
    const uint8_t start[8] =
        {CAN_PROTOCOL_CMD_SESSION_START, 1U, 0U, 0U, 0U, 1U, 0U, 0U};
    const uint8_t command[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 1U, 1U, 0U, 0U, 0U, 0U, 0U};

    (void)CAN_CommandGuard_StartSession(
        &guard, 1U, 0xA5U, 2U, start);

    TEST_ASSERT_EQUAL(CAN_COMMAND_GUARD_REPLAY,
                      CAN_CommandGuard_Check(
                          &guard, 0x5AU, 3U, command));
}

static void test_previous_session_nonce_cannot_be_reopened(void)
{
    const uint8_t first_start[8] =
        {CAN_PROTOCOL_CMD_SESSION_START, 1U, 0U, 0U, 0U, 1U, 0U, 0U};
    const uint8_t second_start[8] =
        {CAN_PROTOCOL_CMD_SESSION_START, 2U, 0U, 0U, 0U, 1U, 0U, 0U};

    (void)CAN_CommandGuard_StartSession(
        &guard, 1U, 0xA5U, 1U, first_start);
    (void)CAN_CommandGuard_StartSession(
        &guard, 2U, 0x5AU, 2U, second_start);

    TEST_ASSERT_EQUAL(
        CAN_COMMAND_GUARD_REPLAY,
        CAN_CommandGuard_StartSession(
            &guard, 1U, 0xA5U, 1U, first_start));
}

static void test_safe_stop_and_log_reads_are_not_privileged(void)
{
    const uint8_t pwm_stop[8] =
        {CAN_PROTOCOL_CMD_PWM_SET, 0U, 0U, 0U, 0U, 90U, 0U, 0U};
    const uint8_t log_info[8] =
        {CAN_PROTOCOL_CMD_LOG_GET_INFO, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t pwm_start[8] =
        {CAN_PROTOCOL_CMD_PWM_SET, 0x10U, 0x27U, 0U, 0U, 90U, 0U, 0U};
    const uint8_t led_off[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 1U, 0U, 0U, 0U, 0U, 0U, 0U};

    TEST_ASSERT_EQUAL_UINT8(0U,
                            CAN_CommandGuard_IsPrivileged(pwm_stop));
    TEST_ASSERT_EQUAL_UINT8(0U,
                            CAN_CommandGuard_IsPrivileged(log_info));
    TEST_ASSERT_EQUAL_UINT8(0U,
                            CAN_CommandGuard_IsPrivileged(led_off));
    TEST_ASSERT_EQUAL_UINT8(1U,
                            CAN_CommandGuard_IsPrivileged(pwm_start));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_command_requires_session);
    RUN_TEST(test_new_session_accepts_first_sequence);
    RUN_TEST(test_duplicate_is_not_accepted_twice);
    RUN_TEST(test_evaluation_does_not_consume_sequence);
    RUN_TEST(test_same_sequence_with_changed_payload_is_replay);
    RUN_TEST(test_delayed_retry_matches_recent_history);
    RUN_TEST(test_old_sequence_is_rejected_after_wrap);
    RUN_TEST(test_command_from_other_session_tag_is_rejected);
    RUN_TEST(test_previous_session_nonce_cannot_be_reopened);
    RUN_TEST(test_safe_stop_and_log_reads_are_not_privileged);
    return UNITY_END();
}
