#include "unity.h"

#include "tic12400_recovery.h"

#include <stdint.h>

static TIC12400_RecoveryState_t state;

void setUp(void)
{
    TIC12400_Recovery_Init(&state);
}

void tearDown(void)
{
}

void test_initial_failure_schedules_bounded_retry(void)
{
    TIC12400_Recovery_RecordInitialResult(&state, 100U, 0U);

    TEST_ASSERT_EQUAL_UINT32(1U, state.offline_events);
    TEST_ASSERT_EQUAL_UINT32(
        TIC12400_RECOVERY_INITIAL_DELAY_MS,
        state.retry_delay_ms);
    TEST_ASSERT_EQUAL_UINT8(1U, state.reinitialization_pending);
    TEST_ASSERT_EQUAL_UINT8(
        0U,
        TIC12400_Recovery_ShouldReinitialize(
            &state,
            100U + TIC12400_RECOVERY_INITIAL_DELAY_MS - 1U));
    TEST_ASSERT_EQUAL_UINT8(
        1U,
        TIC12400_Recovery_ShouldReinitialize(
            &state,
            100U + TIC12400_RECOVERY_INITIAL_DELAY_MS));
}

void test_transient_service_failure_does_not_mark_offline(void)
{
    TEST_ASSERT_EQUAL_UINT8(
        0U,
        TIC12400_Recovery_RecordServiceResult(&state, 10U, 0U));
    TEST_ASSERT_EQUAL_UINT8(
        0U,
        TIC12400_Recovery_RecordServiceResult(&state, 20U, 0U));
    TEST_ASSERT_EQUAL_UINT8(0U, state.reinitialization_pending);

    TEST_ASSERT_EQUAL_UINT8(
        0U,
        TIC12400_Recovery_RecordServiceResult(&state, 30U, 1U));
    TEST_ASSERT_EQUAL_UINT32(0U, state.consecutive_failures);
}

void test_failure_threshold_marks_device_offline_once(void)
{
    (void)TIC12400_Recovery_RecordServiceResult(&state, 10U, 0U);
    (void)TIC12400_Recovery_RecordServiceResult(&state, 20U, 0U);

    TEST_ASSERT_EQUAL_UINT8(
        1U,
        TIC12400_Recovery_RecordServiceResult(&state, 30U, 0U));
    TEST_ASSERT_EQUAL_UINT32(1U, state.offline_events);
    TEST_ASSERT_EQUAL_UINT8(1U, state.reinitialization_pending);

    TEST_ASSERT_EQUAL_UINT8(
        0U,
        TIC12400_Recovery_RecordServiceResult(&state, 40U, 0U));
    TEST_ASSERT_EQUAL_UINT32(1U, state.offline_events);
}

void test_failed_reinitialization_uses_capped_exponential_backoff(void)
{
    uint32_t now = 0U;
    uint32_t expected_delay = TIC12400_RECOVERY_INITIAL_DELAY_MS;
    uint8_t index;

    TIC12400_Recovery_RecordInitialResult(&state, now, 0U);
    for (index = 0U; index < 8U; index++)
    {
        now += expected_delay;
        TIC12400_Recovery_RecordReinitializationResult(
            &state,
            now,
            0U);

        if (expected_delay < TIC12400_RECOVERY_MAX_DELAY_MS)
        {
            expected_delay *= 2U;
            if (expected_delay > TIC12400_RECOVERY_MAX_DELAY_MS)
            {
                expected_delay = TIC12400_RECOVERY_MAX_DELAY_MS;
            }
        }
        TEST_ASSERT_EQUAL_UINT32(
            expected_delay,
            state.retry_delay_ms);
    }

    TEST_ASSERT_EQUAL_UINT32(
        TIC12400_RECOVERY_MAX_DELAY_MS,
        state.retry_delay_ms);
    TEST_ASSERT_EQUAL_UINT32(8U, state.reinitialization_attempts);
}

void test_successful_reinitialization_clears_pending_state(void)
{
    TIC12400_Recovery_RecordInitialResult(&state, 0U, 0U);
    TIC12400_Recovery_RecordReinitializationResult(
        &state,
        TIC12400_RECOVERY_INITIAL_DELAY_MS,
        1U);

    TEST_ASSERT_EQUAL_UINT32(1U, state.reinitialization_attempts);
    TEST_ASSERT_EQUAL_UINT32(1U, state.reinitialization_successes);
    TEST_ASSERT_EQUAL_UINT32(0U, state.consecutive_failures);
    TEST_ASSERT_EQUAL_UINT32(0U, state.retry_delay_ms);
    TEST_ASSERT_EQUAL_UINT8(0U, state.reinitialization_pending);
}

void test_retry_deadline_is_safe_across_tick_wrap(void)
{
    uint32_t start = UINT32_MAX - 100U;

    TIC12400_Recovery_RecordInitialResult(&state, start, 0U);

    TEST_ASSERT_EQUAL_UINT8(
        0U,
        TIC12400_Recovery_ShouldReinitialize(
            &state,
            start + TIC12400_RECOVERY_INITIAL_DELAY_MS - 1U));
    TEST_ASSERT_EQUAL_UINT8(
        1U,
        TIC12400_Recovery_ShouldReinitialize(
            &state,
            start + TIC12400_RECOVERY_INITIAL_DELAY_MS));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_initial_failure_schedules_bounded_retry);
    RUN_TEST(test_transient_service_failure_does_not_mark_offline);
    RUN_TEST(test_failure_threshold_marks_device_offline_once);
    RUN_TEST(
        test_failed_reinitialization_uses_capped_exponential_backoff);
    RUN_TEST(test_successful_reinitialization_clears_pending_state);
    RUN_TEST(test_retry_deadline_is_safe_across_tick_wrap);
    return UNITY_END();
}
