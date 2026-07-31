#include "unity.h"

#include "app_timing.h"

#define TEST_CORE_CLOCK_HZ 64000000UL
#define TEST_CYCLES_PER_US 64UL

void setUp(void)
{
    App_Timing_Init(TEST_CORE_CLOCK_HZ);
}

void tearDown(void)
{
}

void test_service_stats_track_min_max_overrun_and_counter_wrap(void)
{
    App_Timing_Snapshot_t snapshot;
    uint32_t rtc_budget_cycles;

    App_Timing_RecordElapsed(APP_TIMING_SERVICE_CONTROL, 100U, 164U);
    App_Timing_RecordElapsed(
        APP_TIMING_SERVICE_CONTROL,
        UINT32_MAX - 31U,
        32U);

    App_Timing_GetSnapshot(&snapshot);
    TEST_ASSERT_EQUAL_UINT32(
        2U,
        snapshot.service[APP_TIMING_SERVICE_CONTROL].sample_count);
    TEST_ASSERT_EQUAL_UINT32(
        64U,
        snapshot.service[APP_TIMING_SERVICE_CONTROL].minimum_cycles);
    TEST_ASSERT_EQUAL_UINT32(
        64U,
        snapshot.service[APP_TIMING_SERVICE_CONTROL].maximum_cycles);

    rtc_budget_cycles =
        snapshot.service[APP_TIMING_SERVICE_RTC].budget_cycles;
    App_Timing_RecordElapsed(
        APP_TIMING_SERVICE_RTC,
        0U,
        rtc_budget_cycles);
    App_Timing_RecordElapsed(
        APP_TIMING_SERVICE_RTC,
        0U,
        rtc_budget_cycles + 1U);
    App_Timing_GetSnapshot(&snapshot);

    TEST_ASSERT_EQUAL_UINT32(
        1U,
        snapshot.service[APP_TIMING_SERVICE_RTC].overrun_count);
    TEST_ASSERT_EQUAL_UINT32(
        rtc_budget_cycles + 1U,
        snapshot.service[APP_TIMING_SERVICE_RTC].maximum_cycles);
}

void test_ack_histogram_reports_bounded_percentiles(void)
{
    App_Timing_AckSummary_t summary;
    uint32_t sample;

    for (sample = 0U; sample < 50U; sample++)
    {
        App_Timing_RecordAckElapsed(0U, 50U * TEST_CYCLES_PER_US);
    }
    for (sample = 0U; sample < 45U; sample++)
    {
        App_Timing_RecordAckElapsed(0U, 900U * TEST_CYCLES_PER_US);
    }
    for (sample = 0U; sample < 4U; sample++)
    {
        App_Timing_RecordAckElapsed(0U, 4000U * TEST_CYCLES_PER_US);
    }
    App_Timing_RecordAckElapsed(0U, 30000U * TEST_CYCLES_PER_US);

    App_Timing_GetAckSummary(&summary);
    TEST_ASSERT_EQUAL_UINT32(100U, summary.sample_count);
    TEST_ASSERT_EQUAL_UINT32(30000U, summary.current_us);
    TEST_ASSERT_EQUAL_UINT32(100U, summary.p50_us);
    TEST_ASSERT_EQUAL_UINT32(1000U, summary.p95_us);
    TEST_ASSERT_EQUAL_UINT32(5000U, summary.p99_us);
    TEST_ASSERT_EQUAL_UINT32(30000U, summary.maximum_us);
}

void test_disabled_counter_rejects_samples(void)
{
    App_Timing_Snapshot_t snapshot;

    App_Timing_Init(0U);
    App_Timing_RecordElapsed(APP_TIMING_SERVICE_MAIN_LOOP, 0U, 100U);
    App_Timing_RecordAckElapsed(0U, 100U);
    App_Timing_GetSnapshot(&snapshot);

    TEST_ASSERT_EQUAL_UINT8(0U, snapshot.enabled);
    TEST_ASSERT_EQUAL_UINT32(
        0U,
        snapshot.service[APP_TIMING_SERVICE_MAIN_LOOP].sample_count);
    TEST_ASSERT_EQUAL_UINT32(0U, snapshot.ack.sample_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_service_stats_track_min_max_overrun_and_counter_wrap);
    RUN_TEST(test_ack_histogram_reports_bounded_percentiles);
    RUN_TEST(test_disabled_counter_rejects_samples);
    return UNITY_END();
}
