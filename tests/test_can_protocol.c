#include "unity.h"
#include "can_protocol.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Endianness Tests */
void test_ReadU16LE_should_decode_little_endian(void)
{
    uint8_t data[] = {0x34, 0x12};
    TEST_ASSERT_EQUAL_UINT16(0x1234, CAN_Protocol_ReadU16LE(data));
}

void test_ReadU32LE_should_decode_little_endian(void)
{
    uint8_t data[] = {0x78, 0x56, 0x34, 0x12};
    TEST_ASSERT_EQUAL_UINT32(0x12345678, CAN_Protocol_ReadU32LE(data));
}

void test_WriteU32LE_should_encode_little_endian(void)
{
    uint8_t data[4] = {0};
    CAN_Protocol_WriteU32LE(data, 0x12345678);
    TEST_ASSERT_EQUAL_UINT8(0x78, data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x56, data[1]);
    TEST_ASSERT_EQUAL_UINT8(0x34, data[2]);
    TEST_ASSERT_EQUAL_UINT8(0x12, data[3]);
}

/* ID Validation Tests */
void test_IsValidId_standard_max_0x7FF(void)
{
    TEST_ASSERT_TRUE(CAN_Protocol_IsValidId(0, 0x7FF));
    TEST_ASSERT_TRUE(CAN_Protocol_IsValidId(0, 0x000));
}

void test_IsValidId_standard_rejects_0x800(void)
{
    TEST_ASSERT_FALSE(CAN_Protocol_IsValidId(0, 0x800));
    TEST_ASSERT_FALSE(CAN_Protocol_IsValidId(0, 0xFFFFFFFF));
}

void test_IsValidId_extended_max_0x1FFFFFFF(void)
{
    TEST_ASSERT_TRUE(CAN_Protocol_IsValidId(1, 0x1FFFFFFF));
    TEST_ASSERT_TRUE(CAN_Protocol_IsValidId(1, 0x00000000));
}

void test_IsValidId_extended_rejects_0x20000000(void)
{
    TEST_ASSERT_FALSE(CAN_Protocol_IsValidId(1, 0x20000000));
    TEST_ASSERT_FALSE(CAN_Protocol_IsValidId(1, 0xFFFFFFFF));
}

/* System Status Encoding Test */
void test_EncodeSystemStatus_matches_struct(void)
{
    CAN_Protocol_SystemStatus_t status = {0};
    status.slot_1_running = 1;
    status.slot_2_running = 0;
    status.led_1_on = 1;
    status.led_2_on = 0;
    status.request_flags =
        CAN_PROTOCOL_SYSTEM_REQUEST_SLOT_1 |
        CAN_PROTOCOL_SYSTEM_REQUEST_PWM;
    status.physical_flags =
        CAN_PROTOCOL_SYSTEM_PHYSICAL_DATA_VALID |
        CAN_PROTOCOL_SYSTEM_PHYSICAL_IN0_CLOSED |
        CAN_PROTOCOL_SYSTEM_PHYSICAL_IN2_CLOSED;
    status.override_flags =
        CAN_PROTOCOL_SYSTEM_OVERRIDE_PWM_BLOCKED;
    uint8_t payload[8] = {0};
    CAN_Protocol_EncodeSystemStatus(&status, payload);

    /* Flags: bit0=slot1(1), bit1=slot2(0), bit2=led1(1), bit3=led2(0) -> 0x05 */
    TEST_ASSERT_EQUAL_UINT8(0x05, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(1, payload[1]); // slot1
    TEST_ASSERT_EQUAL_UINT8(0, payload[2]); // slot2
    TEST_ASSERT_EQUAL_UINT8(1, payload[3]); // led1
    TEST_ASSERT_EQUAL_UINT8(0, payload[4]); // led2
    TEST_ASSERT_EQUAL_UINT8(0x11, payload[5]);
    TEST_ASSERT_EQUAL_UINT8(0x0B, payload[6]);
    TEST_ASSERT_EQUAL_UINT8(0x10, payload[7]);
}

void test_EncodeCanRxHealth_matches_wire_format(void)
{
    CAN_Protocol_CanRxHealth_t health = {
        .flags = CAN_PROTOCOL_CAN_RX_HEALTH_MESSAGE_LOST |
                 CAN_PROTOCOL_CAN_RX_HEALTH_FIFO_FULL_SEEN,
        .max_fifo_fill = 32U,
        .message_lost_events = 0x1234U,
        .fifo_full_events = 0x5678U,
        .watermark_events = 0x9ABCU
    };
    uint8_t payload[8] = {0};

    CAN_Protocol_EncodeCanRxHealth(&health, payload);

    TEST_ASSERT_EQUAL_HEX8(0x06U, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(32U, payload[1]);
    TEST_ASSERT_EQUAL_HEX8(0x34U, payload[2]);
    TEST_ASSERT_EQUAL_HEX8(0x12U, payload[3]);
    TEST_ASSERT_EQUAL_HEX8(0x78U, payload[4]);
    TEST_ASSERT_EQUAL_HEX8(0x56U, payload[5]);
    TEST_ASSERT_EQUAL_HEX8(0xBCU, payload[6]);
    TEST_ASSERT_EQUAL_HEX8(0x9AU, payload[7]);
}

void test_EncodeTimingTelemetry_matches_wire_format(void)
{
    CAN_Protocol_TimingService_t service = {
        .service_id = 3U,
        .flags = CAN_PROTOCOL_TIMING_SERVICE_ENABLED |
                 CAN_PROTOCOL_TIMING_SERVICE_OVERRUN_LATCHED,
        .current_us = 0x1234U,
        .minimum_us = 0x5678U,
        .maximum_us = 0x9ABCU
    };
    CAN_Protocol_TimingAckLatency_t ack = {
        .p50_us = 100U,
        .p95_us = 1000U,
        .p99_us = 5000U,
        .maximum_us = 30000U
    };
    uint8_t payload[8] = {0};

    CAN_Protocol_EncodeTimingService(&service, payload);
    TEST_ASSERT_EQUAL_HEX8(0x03U, payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0x05U, payload[1]);
    TEST_ASSERT_EQUAL_HEX8(0x34U, payload[2]);
    TEST_ASSERT_EQUAL_HEX8(0x12U, payload[3]);
    TEST_ASSERT_EQUAL_HEX8(0x78U, payload[4]);
    TEST_ASSERT_EQUAL_HEX8(0x56U, payload[5]);
    TEST_ASSERT_EQUAL_HEX8(0xBCU, payload[6]);
    TEST_ASSERT_EQUAL_HEX8(0x9AU, payload[7]);

    CAN_Protocol_EncodeTimingAckLatency(&ack, payload);
    TEST_ASSERT_EQUAL_HEX16(100U, CAN_Protocol_ReadU16LE(&payload[0]));
    TEST_ASSERT_EQUAL_HEX16(1000U, CAN_Protocol_ReadU16LE(&payload[2]));
    TEST_ASSERT_EQUAL_HEX16(5000U, CAN_Protocol_ReadU16LE(&payload[4]));
    TEST_ASSERT_EQUAL_HEX16(30000U, CAN_Protocol_ReadU16LE(&payload[6]));
}

void test_EncodePwmStatus_includes_control_policy(void)
{
    CAN_Protocol_PwmStatus_t status = {
        .running = 0U,
        .duty_percent = 51U,
        .actual_frequency_hz = 321366U,
        .control_flags =
            CAN_PROTOCOL_PWM_CONTROL_REQUESTED |
            CAN_PROTOCOL_PWM_CONTROL_BLOCKED |
            CAN_PROTOCOL_PWM_CONTROL_SWITCH_DATA_VALID,
    };
    uint8_t payload[8] = {0};

    CAN_Protocol_EncodePwmStatus(&status, payload);

    TEST_ASSERT_EQUAL_UINT8(0U, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(51U, payload[1]);
    TEST_ASSERT_EQUAL_UINT8(0x56U, payload[2]);
    TEST_ASSERT_EQUAL_UINT8(0xE7U, payload[3]);
    TEST_ASSERT_EQUAL_UINT8(0x04U, payload[4]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, payload[5]);
    TEST_ASSERT_EQUAL_UINT8(0x0DU, payload[6]);
    TEST_ASSERT_EQUAL_UINT8(0U, payload[7]);
}

void test_EncodePwmSelfTestStatus_matches_wire_format(void)
{
    CAN_Protocol_PwmSelfTestStatus_t status = {
        .state = 1U,
        .current_point = 3U,
        .total_points = 10U,
        .passed_points = 2U,
        .expected_frequency_hz = 100000U,
    };
    uint8_t payload[8] = {0};

    CAN_Protocol_EncodePwmSelfTestStatus(&status, payload);

    TEST_ASSERT_EQUAL_UINT8(1U, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(3U, payload[1]);
    TEST_ASSERT_EQUAL_UINT8(10U, payload[2]);
    TEST_ASSERT_EQUAL_UINT8(2U, payload[3]);
    TEST_ASSERT_EQUAL_UINT8(0xA0U, payload[4]);
    TEST_ASSERT_EQUAL_UINT8(0x86U, payload[5]);
    TEST_ASSERT_EQUAL_UINT8(0x01U, payload[6]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, payload[7]);
}

void test_EncodePwmSelfTestResult_matches_wire_format(void)
{
    CAN_Protocol_PwmSelfTestResult_t result = {
        .point = 3U,
        .passed = 1U,
        .expected_duty_percent = 50U,
        .measured_duty_percent = 49U,
        .measured_frequency_hz = 99999U,
    };
    uint8_t payload[8] = {0};

    CAN_Protocol_EncodePwmSelfTestResult(&result, payload);

    TEST_ASSERT_EQUAL_UINT8(3U, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(1U, payload[1]);
    TEST_ASSERT_EQUAL_UINT8(50U, payload[2]);
    TEST_ASSERT_EQUAL_UINT8(49U, payload[3]);
    TEST_ASSERT_EQUAL_UINT8(0x9FU, payload[4]);
    TEST_ASSERT_EQUAL_UINT8(0x86U, payload[5]);
    TEST_ASSERT_EQUAL_UINT8(0x01U, payload[6]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, payload[7]);
}

void test_EncodeTic12400Status_matches_wire_format(void)
{
    CAN_Protocol_Tic12400Status_t status = {
        .online = 1U,
        .configuration_valid = 1U,
        .crc_complete = 1U,
        .monitoring = 1U,
        .adc_characterization = 1U,
        .por_observed = 1U,
        .service_fault = 0U,
        .device_id = 0x20U,
        .service_result = 0U,
        .spi_fail = 0U,
        .parity_fail = 1U,
        .switch_state_change = 1U,
        .supply_threshold = 0U,
        .temperature = 1U,
        .other_interrupt = 0U,
        .power_on_reset = 1U,
        .service_failures = 0x1234U,
        .last_nonzero_int_status = 0x5678U,
    };
    uint8_t payload[8] = {0};

    CAN_Protocol_EncodeTic12400Status(&status, payload);

    TEST_ASSERT_EQUAL_UINT8(0x3FU, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0x20U, payload[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, payload[2]);
    TEST_ASSERT_EQUAL_UINT8(0x56U, payload[3]);
    TEST_ASSERT_EQUAL_UINT8(0x34U, payload[4]);
    TEST_ASSERT_EQUAL_UINT8(0x12U, payload[5]);
    TEST_ASSERT_EQUAL_UINT8(0x78U, payload[6]);
    TEST_ASSERT_EQUAL_UINT8(0x56U, payload[7]);
}

void test_EncodeTic12400AdcGroup_matches_wire_format(void)
{
    CAN_Protocol_Tic12400AdcGroup_t group = {
        .generation = 0x42U,
        .group_index = 7U,
        .adc_code = {0x027U, 0x003U, 0x3FFU},
    };
    uint8_t payload[8] = {0};

    CAN_Protocol_EncodeTic12400AdcGroup(&group, payload);

    TEST_ASSERT_EQUAL_UINT8(0x42U, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(7U, payload[1]);
    TEST_ASSERT_EQUAL_UINT8(0x27U, payload[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, payload[3]);
    TEST_ASSERT_EQUAL_UINT8(0x03U, payload[4]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, payload[5]);
    TEST_ASSERT_EQUAL_UINT8(0xFFU, payload[6]);
    TEST_ASSERT_EQUAL_UINT8(0x03U, payload[7]);
}

void test_EncodeTic12400SwitchState_matches_wire_format(void)
{
    CAN_Protocol_Tic12400SwitchState_t state = {
        .closed_bitmap = 0x00A1B2C3UL,
        .valid_mask = 0x00FFEFFFUL,
        .generation = 0x42U,
        .data_valid = 1U,
    };
    uint8_t payload[8] = {0};

    CAN_Protocol_EncodeTic12400SwitchState(&state, payload);

    TEST_ASSERT_EQUAL_UINT8(0xC3U, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0xB2U, payload[1]);
    TEST_ASSERT_EQUAL_UINT8(0xA1U, payload[2]);
    TEST_ASSERT_EQUAL_UINT8(0xFFU, payload[3]);
    TEST_ASSERT_EQUAL_UINT8(0xEFU, payload[4]);
    TEST_ASSERT_EQUAL_UINT8(0xFFU, payload[5]);
    TEST_ASSERT_EQUAL_UINT8(0x42U, payload[6]);
    TEST_ASSERT_EQUAL_UINT8(
        CAN_PROTOCOL_TIC12400_SWITCH_DATA_VALID,
        payload[7]);
}

void test_EncodeTic12400Profile_matches_wire_format(void)
{
    CAN_Protocol_Tic12400Profile_t profile = {
        .battery_switch_mask = 0x00000281UL,
        .battery_capable_mask = 0x000003FFUL,
        .generation = 0x23U,
        .configuration_valid = 1U,
    };
    uint8_t payload[8] = {0};

    CAN_Protocol_EncodeTic12400Profile(&profile, payload);

    TEST_ASSERT_EQUAL_UINT8(0x81U, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02U, payload[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, payload[2]);
    TEST_ASSERT_EQUAL_UINT8(0xFFU, payload[3]);
    TEST_ASSERT_EQUAL_UINT8(0x03U, payload[4]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, payload[5]);
    TEST_ASSERT_EQUAL_UINT8(0x23U, payload[6]);
    TEST_ASSERT_EQUAL_UINT8(
        CAN_PROTOCOL_TIC12400_PROFILE_CONFIGURATION_VALID,
        payload[7]);
}

void test_DecodeTic12400PolarityCommand_validates_mask_and_reserved_bytes(
    void)
{
    uint8_t payload[8] = {
        CAN_PROTOCOL_CMD_TIC12400_SET_POLARITY,
        0x81U,
        0x02U,
        0U,
        0U,
        0U,
        0U,
        0U,
    };
    uint32_t battery_switch_mask = 0U;

    TEST_ASSERT_EQUAL_UINT8(
        1U,
        CAN_Protocol_DecodeTic12400PolarityCommand(
            payload, &battery_switch_mask));
    TEST_ASSERT_EQUAL_HEX32(0x00000281UL, battery_switch_mask);

    payload[2] = 0x04U;
    TEST_ASSERT_EQUAL_UINT8(
        0U,
        CAN_Protocol_DecodeTic12400PolarityCommand(
            payload, &battery_switch_mask));
    payload[2] = 0x02U;
    payload[7] = 1U;
    TEST_ASSERT_EQUAL_UINT8(
        0U,
        CAN_Protocol_DecodeTic12400PolarityCommand(
            payload, &battery_switch_mask));
    TEST_ASSERT_EQUAL_UINT8(
        0U,
        CAN_Protocol_DecodeTic12400PolarityCommand(NULL, NULL));
}

void test_CalculateCommandToken_uses_sae_j1850_crc8(void)
{
    const uint8_t payload[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 2U, 1U, 0U, 0U, 0U, 0U, 0U};

    TEST_ASSERT_EQUAL_HEX8(
        0x8CU,
        CAN_Protocol_CalculateCommandToken(payload));
    TEST_ASSERT_EQUAL_HEX8(
        0x00U,
        CAN_Protocol_CalculateCommandToken(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ReadU16LE_should_decode_little_endian);
    RUN_TEST(test_ReadU32LE_should_decode_little_endian);
    RUN_TEST(test_WriteU32LE_should_encode_little_endian);
    RUN_TEST(test_IsValidId_standard_max_0x7FF);
    RUN_TEST(test_IsValidId_standard_rejects_0x800);
    RUN_TEST(test_IsValidId_extended_max_0x1FFFFFFF);
    RUN_TEST(test_IsValidId_extended_rejects_0x20000000);
    RUN_TEST(test_EncodeSystemStatus_matches_struct);
    RUN_TEST(test_EncodeCanRxHealth_matches_wire_format);
    RUN_TEST(test_EncodeTimingTelemetry_matches_wire_format);
    RUN_TEST(test_EncodePwmStatus_includes_control_policy);
    RUN_TEST(test_EncodePwmSelfTestStatus_matches_wire_format);
    RUN_TEST(test_EncodePwmSelfTestResult_matches_wire_format);
    RUN_TEST(test_EncodeTic12400Status_matches_wire_format);
    RUN_TEST(test_EncodeTic12400AdcGroup_matches_wire_format);
    RUN_TEST(test_EncodeTic12400SwitchState_matches_wire_format);
    RUN_TEST(test_EncodeTic12400Profile_matches_wire_format);
    RUN_TEST(
        test_DecodeTic12400PolarityCommand_validates_mask_and_reserved_bytes);
    RUN_TEST(test_CalculateCommandToken_uses_sae_j1850_crc8);
    return UNITY_END();
}
