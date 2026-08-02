#include "unity.h"
#include "tic12400_switch.h"

#define TEST_FITTED_MASK 0x00FFEFFFUL

static TIC12400_SwitchFilter_t filter;

void setUp(void)
{
    TIC12400_SwitchFilter_Init(&filter);
}

void tearDown(void) {}

void test_filter_commits_hardware_debounced_comparator_state(void)
{
    TEST_ASSERT_EQUAL_UINT8(
        1U,
        TIC12400_SwitchFilter_CommitDebouncedMask(
            &filter,
            (1UL << 0) | (1UL << 22),
            TEST_FITTED_MASK));
    TEST_ASSERT_EQUAL_HEX32(
        TEST_FITTED_MASK,
        filter.valid_mask);
    TEST_ASSERT_EQUAL_HEX32(
        (1UL << 0) | (1UL << 22),
        filter.stable_closed_mask);
    TEST_ASSERT_EQUAL_HEX32(0U, filter.last_change_mask);

    TEST_ASSERT_EQUAL_UINT8(
        1U,
        TIC12400_SwitchFilter_CommitDebouncedMask(
            &filter,
            1UL << 22,
            TEST_FITTED_MASK));
    TEST_ASSERT_EQUAL_HEX32(1UL << 0, filter.last_change_mask);
    TEST_ASSERT_EQUAL_UINT8(2U, filter.generation);

    TEST_ASSERT_EQUAL_UINT8(
        0U,
        TIC12400_SwitchFilter_CommitDebouncedMask(
            &filter,
            1UL << 22,
            TEST_FITTED_MASK));
}

void test_filter_masks_unfitted_state_and_handles_generation_wrap(void)
{
    filter.generation = 0xFFU;
    TIC12400_SwitchFilter_CommitDebouncedMask(
        &filter,
        1UL << 12,
        TEST_FITTED_MASK);

    TEST_ASSERT_BITS(
        1UL << 12,
        0U,
        filter.stable_closed_mask);
    TEST_ASSERT_BITS(1UL << 12, 0U, filter.valid_mask);
    TEST_ASSERT_EQUAL_UINT8(0U, filter.generation);
    TEST_ASSERT_EQUAL_UINT8(
        0U,
        TIC12400_SwitchFilter_CommitDebouncedMask(
            NULL,
            0U,
            TEST_FITTED_MASK));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(
        test_filter_commits_hardware_debounced_comparator_state);
    RUN_TEST(
        test_filter_masks_unfitted_state_and_handles_generation_wrap);
    return UNITY_END();
}
