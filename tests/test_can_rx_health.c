#include "unity.h"

#include "can_rx_health.h"

void setUp(void)
{
    CAN_RxHealth_Init();
}

void tearDown(void)
{
}

void test_burst_tracks_notifications_watermark_and_peak_fill(void)
{
    CAN_RxHealth_Stats_t stats;
    uint32_t fill_level;

    for (fill_level = 1U; fill_level <= 32U; fill_level++)
    {
        uint32_t events = CAN_RX_HEALTH_EVENT_NEW_MESSAGE;

        if (fill_level == 24U)
        {
            events |= CAN_RX_HEALTH_EVENT_WATERMARK;
        }

        CAN_RxHealth_RecordIsr(events, fill_level);
    }

    CAN_RxHealth_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(32U, stats.new_message_events);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.watermark_events);
    TEST_ASSERT_EQUAL_UINT32(0U, stats.full_events);
    TEST_ASSERT_EQUAL_UINT32(0U, stats.message_lost_events);
    TEST_ASSERT_EQUAL_UINT32(32U, stats.max_fill_level);
}

void test_capacity_overrun_records_full_and_message_lost(void)
{
    CAN_RxHealth_Stats_t stats;

    CAN_RxHealth_RecordIsr(
        CAN_RX_HEALTH_EVENT_NEW_MESSAGE |
        CAN_RX_HEALTH_EVENT_FULL,
        32U);
    CAN_RxHealth_RecordIsr(
        CAN_RX_HEALTH_EVENT_MESSAGE_LOST,
        32U);

    CAN_RxHealth_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.new_message_events);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.full_events);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.message_lost_events);
    TEST_ASSERT_EQUAL_UINT32(32U, stats.max_fill_level);
}

void test_init_clears_latched_counters(void)
{
    CAN_RxHealth_Stats_t stats;

    CAN_RxHealth_RecordIsr(
        CAN_RX_HEALTH_EVENT_WATERMARK |
        CAN_RX_HEALTH_EVENT_FULL |
        CAN_RX_HEALTH_EVENT_MESSAGE_LOST,
        32U);
    CAN_RxHealth_Init();
    CAN_RxHealth_GetStats(&stats);

    TEST_ASSERT_EQUAL_UINT32(0U, stats.new_message_events);
    TEST_ASSERT_EQUAL_UINT32(0U, stats.watermark_events);
    TEST_ASSERT_EQUAL_UINT32(0U, stats.full_events);
    TEST_ASSERT_EQUAL_UINT32(0U, stats.message_lost_events);
    TEST_ASSERT_EQUAL_UINT32(0U, stats.max_fill_level);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_burst_tracks_notifications_watermark_and_peak_fill);
    RUN_TEST(test_capacity_overrun_records_full_and_message_lost);
    RUN_TEST(test_init_clears_latched_counters);
    return UNITY_END();
}
