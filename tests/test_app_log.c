#include "unity.h"

#include "app_log.h"

static uint32_t fake_tick;

uint32_t HAL_GetTick(void)
{
    return fake_tick;
}

void setUp(void)
{
}

void tearDown(void)
{
}

static void reset_log(void)
{
    App_Log_Init();
    App_Log_Clear();
    App_Log_Init();
    fake_tick = 0U;
}

static void test_records_survive_reinitialization(void)
{
    App_Log_Record_t record;
    App_Log_Debug_t debug;

    reset_log();
    fake_tick = 123U;
    TEST_ASSERT_EQUAL_UINT8(
        1U,
        App_Log_Push(APP_LOG_SOURCE_PWM,
                     APP_LOG_SEVERITY_INFO,
                     APP_LOG_EVENT_PWM_SELF_TEST_COMPLETED,
                     0x010A0A02UL,
                     0U));

    App_Log_Init();

    TEST_ASSERT_EQUAL_UINT16(1U, App_Log_GetCount());
    TEST_ASSERT_EQUAL_UINT8(1U, App_Log_ReadBySequence(1U, &record));
    TEST_ASSERT_EQUAL_UINT32(123U, record.uptime_ms);
    TEST_ASSERT_EQUAL_UINT16(
        APP_LOG_EVENT_PWM_SELF_TEST_COMPLETED,
        record.event_code);
    App_Log_GetDebug(&debug);
    TEST_ASSERT_EQUAL_UINT16(1U, debug.recovered_count);
    TEST_ASSERT_EQUAL_UINT8(1U, debug.storage_ready);
}

static void test_corrupt_newest_record_is_not_recovered(void)
{
    App_Log_Info_t info;
    uint16_t index;

    reset_log();
    (void)App_Log_Push(APP_LOG_SOURCE_SYSTEM,
                       APP_LOG_SEVERITY_INFO,
                       APP_LOG_EVENT_SYSTEM_BOOT,
                       0U,
                       0U);
    (void)App_Log_Push(APP_LOG_SOURCE_CAN,
                       APP_LOG_SEVERITY_WARNING,
                       APP_LOG_EVENT_CAN_RX_REJECTED,
                       1U,
                       2U);

    for (index = 0U; index < APP_LOG_RAM_CAPACITY; index++)
    {
        if (g_appLogRecords[index].record.sequence == 2U)
        {
            g_appLogRecords[index].record.crc16 ^= 1U;
        }
    }

    App_Log_Init();
    App_Log_GetInfo(&info);
    TEST_ASSERT_EQUAL_UINT16(1U, info.count);
    TEST_ASSERT_EQUAL_UINT32(1U, info.oldest_sequence);
    TEST_ASSERT_EQUAL_UINT32(1U, info.latest_sequence);
}

static void test_full_ring_recovers_latest_capacity(void)
{
    App_Log_Info_t info;
    uint32_t sequence;

    reset_log();
    for (sequence = 1U; sequence <= 70U; sequence++)
    {
        TEST_ASSERT_EQUAL_UINT8(
            1U,
            App_Log_Push(APP_LOG_SOURCE_SYSTEM,
                         APP_LOG_SEVERITY_INFO,
                         APP_LOG_EVENT_SYSTEM_BOOT,
                         sequence,
                         0U));
    }

    App_Log_Init();
    App_Log_GetInfo(&info);

    TEST_ASSERT_EQUAL_UINT16(APP_LOG_RAM_CAPACITY, info.count);
    TEST_ASSERT_EQUAL_UINT32(7U, info.oldest_sequence);
    TEST_ASSERT_EQUAL_UINT32(70U, info.latest_sequence);
}

static void test_clear_prevents_old_records_from_returning(void)
{
    reset_log();
    (void)App_Log_Push(APP_LOG_SOURCE_SYSTEM,
                       APP_LOG_SEVERITY_INFO,
                       APP_LOG_EVENT_SYSTEM_BOOT,
                       0U,
                       0U);

    App_Log_Clear();
    App_Log_Init();

    TEST_ASSERT_EQUAL_UINT16(0U, App_Log_GetCount());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_records_survive_reinitialization);
    RUN_TEST(test_corrupt_newest_record_is_not_recovered);
    RUN_TEST(test_full_ring_recovers_latest_capacity);
    RUN_TEST(test_clear_prevents_old_records_from_returning);
    return UNITY_END();
}
