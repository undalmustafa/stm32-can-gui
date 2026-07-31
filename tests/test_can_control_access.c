#include "unity.h"

#include "can_control_access.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_init_and_null_inputs_are_safe(void)
{
    CAN_ControlAccess_t state =
    {
        .request_count = 1U,
        .handled_request_count = 0U,
        .active = 1U,
        .deadline = 123U
    };

    CAN_ControlAccess_Init(&state);
    TEST_ASSERT_EQUAL_UINT32(0U, state.request_count);
    TEST_ASSERT_EQUAL_UINT32(0U, state.handled_request_count);
    TEST_ASSERT_EQUAL_UINT8(0U, state.active);
    TEST_ASSERT_EQUAL_UINT32(0U, state.deadline);
    TEST_ASSERT_EQUAL_UINT8(0U, CAN_ControlAccess_IsOpen(&state));
    TEST_ASSERT_EQUAL_UINT32(0U,
                             CAN_ControlAccess_RemainingMs(&state, 0U));

    CAN_ControlAccess_Init(NULL);
    CAN_ControlAccess_RequestFromIsr(NULL);
    TEST_ASSERT_EQUAL(CAN_CONTROL_ACCESS_NO_CHANGE,
                      CAN_ControlAccess_Update(NULL, 0U));
    TEST_ASSERT_EQUAL_UINT8(0U, CAN_ControlAccess_IsOpen(NULL));
    TEST_ASSERT_EQUAL_UINT32(0U,
                             CAN_ControlAccess_RemainingMs(NULL, 0U));
}

void test_request_is_deferred_until_main_loop_update(void)
{
    CAN_ControlAccess_t state;

    CAN_ControlAccess_Init(&state);
    CAN_ControlAccess_RequestFromIsr(&state);

    TEST_ASSERT_EQUAL_UINT32(1U, state.request_count);
    TEST_ASSERT_EQUAL_UINT32(0U, state.handled_request_count);
    TEST_ASSERT_EQUAL_UINT8(0U, CAN_ControlAccess_IsOpen(&state));
    TEST_ASSERT_EQUAL(CAN_CONTROL_ACCESS_OPENED,
                      CAN_ControlAccess_Update(&state, 1000U));
    TEST_ASSERT_EQUAL_UINT32(1U, state.handled_request_count);
    TEST_ASSERT_EQUAL_UINT8(1U, CAN_ControlAccess_IsOpen(&state));
    TEST_ASSERT_EQUAL_UINT32(1000U + CAN_CONTROL_ACCESS_WINDOW_MS,
                             state.deadline);
    TEST_ASSERT_EQUAL_UINT32(
        CAN_CONTROL_ACCESS_WINDOW_MS,
        CAN_ControlAccess_RemainingMs(&state, 1000U));
}

void test_queries_are_side_effect_free(void)
{
    CAN_ControlAccess_t state;
    CAN_ControlAccess_t before;

    CAN_ControlAccess_Init(&state);
    CAN_ControlAccess_RequestFromIsr(&state);
    (void)CAN_ControlAccess_Update(&state, 10U);
    before = state;

    TEST_ASSERT_EQUAL_UINT8(1U, CAN_ControlAccess_IsOpen(&state));
    TEST_ASSERT_EQUAL_UINT32(CAN_CONTROL_ACCESS_WINDOW_MS - 5U,
                             CAN_ControlAccess_RemainingMs(&state, 15U));
    TEST_ASSERT_EQUAL_UINT32(before.request_count, state.request_count);
    TEST_ASSERT_EQUAL_UINT32(before.handled_request_count,
                             state.handled_request_count);
    TEST_ASSERT_EQUAL_UINT8(before.active, state.active);
    TEST_ASSERT_EQUAL_UINT32(before.deadline, state.deadline);
}

void test_window_expires_exactly_at_deadline(void)
{
    CAN_ControlAccess_t state;

    CAN_ControlAccess_Init(&state);
    CAN_ControlAccess_RequestFromIsr(&state);
    (void)CAN_ControlAccess_Update(&state, 100U);

    TEST_ASSERT_EQUAL(CAN_CONTROL_ACCESS_NO_CHANGE,
                      CAN_ControlAccess_Update(
                          &state,
                          100U + CAN_CONTROL_ACCESS_WINDOW_MS - 1U));
    TEST_ASSERT_EQUAL_UINT8(1U, CAN_ControlAccess_IsOpen(&state));
    TEST_ASSERT_EQUAL_UINT32(1U,
                             CAN_ControlAccess_RemainingMs(
                                 &state,
                                 100U + CAN_CONTROL_ACCESS_WINDOW_MS - 1U));

    TEST_ASSERT_EQUAL(CAN_CONTROL_ACCESS_EXPIRED,
                      CAN_ControlAccess_Update(
                          &state,
                          100U + CAN_CONTROL_ACCESS_WINDOW_MS));
    TEST_ASSERT_EQUAL_UINT8(0U, CAN_ControlAccess_IsOpen(&state));
}

void test_repeated_request_restarts_full_window(void)
{
    CAN_ControlAccess_t state;
    uint32_t first_deadline;

    CAN_ControlAccess_Init(&state);
    CAN_ControlAccess_RequestFromIsr(&state);
    (void)CAN_ControlAccess_Update(&state, 100U);
    first_deadline = state.deadline;

    CAN_ControlAccess_RequestFromIsr(&state);
    TEST_ASSERT_EQUAL(CAN_CONTROL_ACCESS_OPENED,
                      CAN_ControlAccess_Update(&state, 200U));
    TEST_ASSERT_EQUAL_UINT32(200U + CAN_CONTROL_ACCESS_WINDOW_MS,
                             state.deadline);
    TEST_ASSERT_TRUE(state.deadline > first_deadline);
}

void test_deadline_comparison_is_safe_across_tick_wrap(void)
{
    CAN_ControlAccess_t state;
    uint32_t start = UINT32_MAX - 100U;
    uint32_t before_deadline = start + CAN_CONTROL_ACCESS_WINDOW_MS - 1U;
    uint32_t deadline = start + CAN_CONTROL_ACCESS_WINDOW_MS;

    CAN_ControlAccess_Init(&state);
    CAN_ControlAccess_RequestFromIsr(&state);
    TEST_ASSERT_EQUAL(CAN_CONTROL_ACCESS_OPENED,
                      CAN_ControlAccess_Update(&state, start));
    TEST_ASSERT_EQUAL_UINT32(deadline, state.deadline);
    TEST_ASSERT_EQUAL(CAN_CONTROL_ACCESS_NO_CHANGE,
                      CAN_ControlAccess_Update(&state, before_deadline));
    TEST_ASSERT_EQUAL_UINT32(1U,
                             CAN_ControlAccess_RemainingMs(
                                 &state, before_deadline));
    TEST_ASSERT_EQUAL(CAN_CONTROL_ACCESS_EXPIRED,
                      CAN_ControlAccess_Update(&state, deadline));
}

void test_request_counter_wrap_is_observed_once(void)
{
    CAN_ControlAccess_t state;

    CAN_ControlAccess_Init(&state);
    state.request_count = UINT32_MAX;
    state.handled_request_count = UINT32_MAX;

    CAN_ControlAccess_RequestFromIsr(&state);
    TEST_ASSERT_EQUAL_UINT32(0U, state.request_count);
    TEST_ASSERT_EQUAL(CAN_CONTROL_ACCESS_OPENED,
                      CAN_ControlAccess_Update(&state, 50U));
    TEST_ASSERT_EQUAL_UINT32(0U, state.handled_request_count);
    TEST_ASSERT_EQUAL(CAN_CONTROL_ACCESS_NO_CHANGE,
                      CAN_ControlAccess_Update(&state, 50U));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_and_null_inputs_are_safe);
    RUN_TEST(test_request_is_deferred_until_main_loop_update);
    RUN_TEST(test_queries_are_side_effect_free);
    RUN_TEST(test_window_expires_exactly_at_deadline);
    RUN_TEST(test_repeated_request_restarts_full_window);
    RUN_TEST(test_deadline_comparison_is_safe_across_tick_wrap);
    RUN_TEST(test_request_counter_wrap_is_observed_once);
    return UNITY_END();
}
