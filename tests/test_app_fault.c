#include "unity.h"

#include "app_fault.h"

void setUp(void)
{
    App_Fault_TestResetStorage();
}

void tearDown(void)
{
}

static void test_exception_context_survives_boot_capture(void)
{
    const uint32_t stack_frame[8] = {
        0x00000010UL,
        0x00000011UL,
        0x00000012UL,
        0x00000013UL,
        0x0000001CUL,
        0x08001235UL,
        0x08004567UL,
        0x21000000UL
    };
    App_Fault_Snapshot_t snapshot;

    App_Fault_CaptureBoot();
    App_Fault_RecordExceptionFromIsr(
        APP_FAULT_HARD,
        stack_frame,
        0xFFFFFFF9UL);

    /* Simulate startup after NVIC_SystemReset(). */
    App_Fault_CaptureBoot();
    App_Fault_GetSnapshot(&snapshot);

    TEST_ASSERT_EQUAL_UINT8(1U, snapshot.valid);
    TEST_ASSERT_EQUAL_UINT32(APP_FAULT_HARD, snapshot.type);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFF9UL, snapshot.exception_return);
    TEST_ASSERT_EQUAL_UINT32(0x00000010UL, snapshot.r0);
    TEST_ASSERT_EQUAL_UINT32(0x0000001CUL, snapshot.r12);
    TEST_ASSERT_EQUAL_UINT32(0x08001235UL, snapshot.lr);
    TEST_ASSERT_EQUAL_UINT32(0x08004567UL, snapshot.pc);
    TEST_ASSERT_EQUAL_UINT32(0x21000000UL, snapshot.xpsr);
}

static void test_captured_record_is_consumed_once(void)
{
    App_Fault_Snapshot_t snapshot;

    App_Fault_CaptureBoot();
    App_Fault_RecordExceptionFromIsr(
        APP_FAULT_ERROR_HANDLER,
        NULL,
        0x08000101UL);
    App_Fault_CaptureBoot();
    App_Fault_CaptureBoot();
    App_Fault_GetSnapshot(&snapshot);

    TEST_ASSERT_EQUAL_UINT8(0U, snapshot.valid);
    TEST_ASSERT_EQUAL_UINT8(1U, snapshot.storage_ready);
}

static void test_corrupt_committed_record_is_rejected(void)
{
    App_Fault_Snapshot_t snapshot;

    App_Fault_CaptureBoot();
    App_Fault_RecordExceptionFromIsr(
        APP_FAULT_USAGE,
        NULL,
        0x08000201UL);
    App_Fault_TestCorruptChecksum();
    App_Fault_CaptureBoot();
    App_Fault_GetSnapshot(&snapshot);

    TEST_ASSERT_EQUAL_UINT8(0U, snapshot.valid);
    TEST_ASSERT_EQUAL_UINT32(1U, snapshot.validation_error_count);
}

static void test_invalid_fault_type_is_not_committed(void)
{
    App_Fault_Snapshot_t snapshot;

    App_Fault_CaptureBoot();
    App_Fault_RecordExceptionFromIsr(APP_FAULT_NONE, NULL, 0U);
    App_Fault_CaptureBoot();
    App_Fault_GetSnapshot(&snapshot);

    TEST_ASSERT_EQUAL_UINT8(0U, snapshot.valid);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_exception_context_survives_boot_capture);
    RUN_TEST(test_captured_record_is_consumed_once);
    RUN_TEST(test_corrupt_committed_record_is_rejected);
    RUN_TEST(test_invalid_fault_type_is_not_committed);
    return UNITY_END();
}
