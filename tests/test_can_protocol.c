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
    uint8_t payload[8] = {0};
    CAN_Protocol_EncodeSystemStatus(&status, payload);

    /* Flags: bit0=slot1(1), bit1=slot2(0), bit2=led1(1), bit3=led2(0) -> 0x05 */
    TEST_ASSERT_EQUAL_UINT8(0x05, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(1, payload[1]); // slot1
    TEST_ASSERT_EQUAL_UINT8(0, payload[2]); // slot2
    TEST_ASSERT_EQUAL_UINT8(1, payload[3]); // led1
    TEST_ASSERT_EQUAL_UINT8(0, payload[4]); // led2
    TEST_ASSERT_EQUAL_UINT8(0, payload[5]);
    TEST_ASSERT_EQUAL_UINT8(0, payload[6]);
    TEST_ASSERT_EQUAL_UINT8(0, payload[7]);
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
    RUN_TEST(test_EncodePwmSelfTestStatus_matches_wire_format);
    RUN_TEST(test_EncodePwmSelfTestResult_matches_wire_format);
    return UNITY_END();
}
