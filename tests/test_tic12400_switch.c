#include "unity.h"
#include "tic12400_switch.h"

#include <string.h>

#define TEST_FITTED_MASK 0x00FFEFFFUL

static TIC12400_SwitchFilter_t filter;
static uint16_t adc_code[TIC12400_SWITCH_CHANNEL_COUNT];

void setUp(void)
{
    TIC12400_SwitchFilter_Init(&filter);
    for (uint8_t channel = 0U;
         channel < TIC12400_SWITCH_CHANNEL_COUNT;
         channel++)
    {
        adc_code[channel] = 1023U;
    }
}

void tearDown(void) {}

static void update_three_times(void)
{
    TIC12400_SwitchFilter_Update(&filter, adc_code, TEST_FITTED_MASK);
    TIC12400_SwitchFilter_Update(&filter, adc_code, TEST_FITTED_MASK);
    TIC12400_SwitchFilter_Update(&filter, adc_code, TEST_FITTED_MASK);
}

void test_filter_requires_three_equal_samples_for_initial_state(void)
{
    adc_code[0] = 40U;

    TEST_ASSERT_EQUAL_UINT8(
        0U,
        TIC12400_SwitchFilter_Update(
            &filter, adc_code, TEST_FITTED_MASK));
    TEST_ASSERT_EQUAL_UINT32(0U, filter.valid_mask);

    TIC12400_SwitchFilter_Update(
        &filter, adc_code, TEST_FITTED_MASK);
    TEST_ASSERT_EQUAL_UINT32(0U, filter.valid_mask);

    TEST_ASSERT_EQUAL_UINT8(
        1U,
        TIC12400_SwitchFilter_Update(
            &filter, adc_code, TEST_FITTED_MASK));
    TEST_ASSERT_EQUAL_UINT32(TEST_FITTED_MASK, filter.valid_mask);
    TEST_ASSERT_EQUAL_UINT32(1UL, filter.stable_closed_mask);
    TEST_ASSERT_EQUAL_UINT8(1U, filter.generation);
    TEST_ASSERT_EQUAL_UINT32(0U, filter.last_change_mask);
}

void test_filter_treats_center_and_right_full_scale_as_open(void)
{
    adc_code[0] = 1023U;
    adc_code[1] = 1023U;
    update_three_times();

    TEST_ASSERT_EQUAL_UINT32(0U, filter.stable_closed_mask);
    TEST_ASSERT_EQUAL_UINT32(TEST_FITTED_MASK, filter.valid_mask);
}

void test_filter_uses_measured_half_scale_fail_safe_boundary(void)
{
    adc_code[0] = TIC12400_SWITCH_CLOSED_MAX_ADC_CODE;
    adc_code[1] = TIC12400_SWITCH_CLOSED_MAX_ADC_CODE + 1U;
    update_three_times();

    TEST_ASSERT_BITS(1UL << 0, 1UL << 0, filter.stable_closed_mask);
    TEST_ASSERT_BITS(1UL << 1, 0U, filter.stable_closed_mask);
}

void test_filter_rejects_bounce_and_reports_stable_change(void)
{
    update_three_times();

    adc_code[0] = 40U;
    TIC12400_SwitchFilter_Update(
        &filter, adc_code, TEST_FITTED_MASK);
    adc_code[0] = 1023U;
    TIC12400_SwitchFilter_Update(
        &filter, adc_code, TEST_FITTED_MASK);
    adc_code[0] = 40U;
    TIC12400_SwitchFilter_Update(
        &filter, adc_code, TEST_FITTED_MASK);
    TEST_ASSERT_EQUAL_UINT32(0U, filter.stable_closed_mask);

    TIC12400_SwitchFilter_Update(
        &filter, adc_code, TEST_FITTED_MASK);
    TEST_ASSERT_EQUAL_UINT8(
        1U,
        TIC12400_SwitchFilter_Update(
            &filter, adc_code, TEST_FITTED_MASK));
    TEST_ASSERT_BITS(1UL, 1UL, filter.stable_closed_mask);
    TEST_ASSERT_EQUAL_UINT32(1UL, filter.last_change_mask);
    TEST_ASSERT_EQUAL_UINT8(2U, filter.generation);
}

void test_filter_never_marks_unfitted_in12_valid_or_closed(void)
{
    adc_code[12] = 0U;
    update_three_times();

    TEST_ASSERT_BITS(1UL << 12, 0U, filter.valid_mask);
    TEST_ASSERT_BITS(1UL << 12, 0U, filter.stable_closed_mask);
}

void test_filter_handles_generation_wrap(void)
{
    filter.generation = 0xFFU;
    update_three_times();

    TEST_ASSERT_EQUAL_UINT8(0U, filter.generation);
}

void test_filter_rejects_null_arguments(void)
{
    TEST_ASSERT_EQUAL_UINT8(
        0U,
        TIC12400_SwitchFilter_Update(
            NULL, adc_code, TEST_FITTED_MASK));
    TEST_ASSERT_EQUAL_UINT8(
        0U,
        TIC12400_SwitchFilter_Update(
            &filter, NULL, TEST_FITTED_MASK));
}

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
}

void test_filter_masks_unfitted_hardware_state(void)
{
    TIC12400_SwitchFilter_CommitDebouncedMask(
        &filter,
        1UL << 12,
        TEST_FITTED_MASK);

    TEST_ASSERT_BITS(
        1UL << 12,
        0U,
        filter.stable_closed_mask);
    TEST_ASSERT_BITS(1UL << 12, 0U, filter.valid_mask);
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
    RUN_TEST(test_filter_requires_three_equal_samples_for_initial_state);
    RUN_TEST(test_filter_treats_center_and_right_full_scale_as_open);
    RUN_TEST(test_filter_uses_measured_half_scale_fail_safe_boundary);
    RUN_TEST(test_filter_rejects_bounce_and_reports_stable_change);
    RUN_TEST(test_filter_never_marks_unfitted_in12_valid_or_closed);
    RUN_TEST(test_filter_handles_generation_wrap);
    RUN_TEST(test_filter_rejects_null_arguments);
    RUN_TEST(
        test_filter_commits_hardware_debounced_comparator_state);
    RUN_TEST(test_filter_masks_unfitted_hardware_state);
    return UNITY_END();
}
