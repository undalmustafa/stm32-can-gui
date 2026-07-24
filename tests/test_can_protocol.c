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
    status.protocol_version = CAN_PROTOCOL_VERSION;

    uint8_t payload[8] = {0};
    CAN_Protocol_EncodeSystemStatus(&status, payload);

    /* Flags: bit0=slot1(1), bit1=slot2(0), bit2=led1(1), bit3=led2(0) -> 0x05 */
    TEST_ASSERT_EQUAL_UINT8(0x05, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(1, payload[1]); // slot1
    TEST_ASSERT_EQUAL_UINT8(0, payload[2]); // slot2
    TEST_ASSERT_EQUAL_UINT8(1, payload[3]); // led1
    TEST_ASSERT_EQUAL_UINT8(0, payload[4]); // led2
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_VERSION, payload[5]); // protocol_version
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
    return UNITY_END();
}
