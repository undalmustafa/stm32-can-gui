#include "unity.h"

#include "can_command_validation.h"

static void expect_valid(const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    TEST_ASSERT_EQUAL(CAN_COMMAND_VALIDATION_VALID,
                      CAN_CommandValidation_Validate(payload));
}

static void expect_invalid(const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    TEST_ASSERT_EQUAL(CAN_COMMAND_VALIDATION_INVALID_PAYLOAD,
                      CAN_CommandValidation_Validate(payload));
}

void setUp(void)
{
}

void tearDown(void)
{
}

void test_slot_configuration_validates_flags_id_and_cycle(void)
{
    uint8_t payload[8] =
        {CAN_PROTOCOL_CMD_SET_SLOT_1, 0U, 0xFFU, 0x07U, 0U, 0U, 1U, 0U};

    expect_valid(payload);
    payload[1] = 0x80U;
    expect_invalid(payload);
    payload[1] = 0U;
    payload[3] = 0x08U;
    expect_invalid(payload);
    payload[1] = CAN_PROTOCOL_SLOT_FLAG_EXTENDED_ID;
    payload[2] = 0xFFU;
    payload[3] = 0xFFU;
    payload[4] = 0xFFU;
    payload[5] = 0x1FU;
    expect_valid(payload);
    payload[6] = 0U;
    payload[7] = 0U;
    expect_invalid(payload);
}

void test_counter_command_requires_reserved_bytes_zero(void)
{
    uint8_t payload[8] =
        {CAN_PROTOCOL_CMD_START_SLOT_2_COUNTER,
         0U, 1U, 0U, 0U, 0U, 0U, 0U};

    expect_valid(payload);
    payload[1] = 1U;
    expect_invalid(payload);
    payload[1] = 0U;
    payload[7] = 1U;
    expect_invalid(payload);
}

void test_led_command_validates_channel_state_and_reserved_bytes(void)
{
    uint8_t payload[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 2U, 1U, 0U, 0U, 0U, 0U, 0U};

    expect_valid(payload);
    payload[1] = 0U;
    expect_invalid(payload);
    payload[1] = 2U;
    payload[2] = 2U;
    expect_invalid(payload);
    payload[2] = 1U;
    payload[4] = 1U;
    expect_invalid(payload);
}

void test_rtc_time_and_datetime_validate_calendar_boundaries(void)
{
    uint8_t time_payload[8] =
        {CAN_PROTOCOL_CMD_RTC_SET_TIME, 23U, 59U, 59U, 0U, 0U, 0U, 0U};
    uint8_t datetime_payload[8] =
        {CAN_PROTOCOL_CMD_RTC_SET_DATETIME,
         99U, 59U, 59U, 23U, 29U, (4U << 5) | 2U, 24U};

    expect_valid(time_payload);
    time_payload[1] = 24U;
    expect_invalid(time_payload);
    time_payload[1] = 23U;
    time_payload[4] = 1U;
    expect_invalid(time_payload);

    expect_valid(datetime_payload);
    datetime_payload[5] = 30U;
    expect_invalid(datetime_payload);
}

void test_alarm_command_obeys_enable_mask_and_field_ranges(void)
{
    uint8_t payload[8] =
        {CAN_PROTOCOL_CMD_RTC_SET_ALARM,
         CAN_PROTOCOL_RTC_ALARM_ENABLE_MASK,
         59U, 59U, 23U, 31U, 6U, 0U};

    expect_valid(payload);
    payload[2] = 60U;
    expect_invalid(payload);
    payload[2] = 59U;
    payload[1] = 0U;
    expect_invalid(payload);
}

void test_log_commands_validate_sequence_and_reserved_bytes(void)
{
    uint8_t info[8] =
        {CAN_PROTOCOL_CMD_LOG_GET_INFO, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    uint8_t read[8] =
        {CAN_PROTOCOL_CMD_LOG_READ_SEQUENCE, 1U, 0U, 0U, 0U, 0U, 0U, 0U};

    expect_valid(info);
    info[7] = 1U;
    expect_invalid(info);
    expect_valid(read);
    read[1] = 0U;
    expect_invalid(read);
    read[1] = 1U;
    read[5] = 1U;
    expect_invalid(read);
}

void test_pwm_commands_validate_frequency_duty_and_action(void)
{
    uint8_t pwm[8] =
        {CAN_PROTOCOL_CMD_PWM_SET, 0x40U, 0x42U, 0x0FU, 0U, 100U, 0U, 0U};
    uint8_t self_test[8] =
        {CAN_PROTOCOL_CMD_PWM_SELF_TEST, 1U, 0U, 0U, 0U, 0U, 0U, 0U};

    expect_valid(pwm);
    pwm[1] = 0x41U;
    expect_invalid(pwm);
    pwm[1] = 0U;
    pwm[2] = 0U;
    pwm[3] = 0U;
    pwm[5] = 101U;
    expect_invalid(pwm);

    expect_valid(self_test);
    self_test[1] = 2U;
    expect_invalid(self_test);
    self_test[1] = 1U;
    self_test[2] = 1U;
    expect_invalid(self_test);
}

void test_polarity_command_validates_mask_and_reserved_bytes(void)
{
    uint8_t payload[8] =
        {CAN_PROTOCOL_CMD_TIC12400_SET_POLARITY,
         0x81U, 0x02U, 0U, 0U, 0U, 0U, 0U};

    expect_valid(payload);
    payload[2] = 0x04U;
    expect_invalid(payload);
    payload[2] = 0x02U;
    payload[7] = 1U;
    expect_invalid(payload);
}

void test_unknown_and_null_payloads_are_rejected_conservatively(void)
{
    const uint8_t unknown[8] =
        {0xFFU, 0U, 0U, 0U, 0U, 0U, 0U, 0U};

    TEST_ASSERT_EQUAL(CAN_COMMAND_VALIDATION_UNKNOWN,
                      CAN_CommandValidation_Validate(unknown));
    expect_invalid(NULL);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_slot_configuration_validates_flags_id_and_cycle);
    RUN_TEST(test_counter_command_requires_reserved_bytes_zero);
    RUN_TEST(test_led_command_validates_channel_state_and_reserved_bytes);
    RUN_TEST(test_rtc_time_and_datetime_validate_calendar_boundaries);
    RUN_TEST(test_alarm_command_obeys_enable_mask_and_field_ranges);
    RUN_TEST(test_log_commands_validate_sequence_and_reserved_bytes);
    RUN_TEST(test_pwm_commands_validate_frequency_duty_and_action);
    RUN_TEST(test_polarity_command_validates_mask_and_reserved_bytes);
    RUN_TEST(test_unknown_and_null_payloads_are_rejected_conservatively);
    return UNITY_END();
}
