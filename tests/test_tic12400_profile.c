#include "unity.h"

#include "tic12400_profile.h"

static TIC12400_Profile_t profile;

void setUp(void)
{
    profile = TIC12400_Profile_CarrierBinary();
}

void tearDown(void)
{
}

void test_carrier_profile_uses_fitted_ground_comparator_inputs(void)
{
    TEST_ASSERT_EQUAL_HEX32(
        TIC12400_PROFILE_CARRIER_FITTED_MASK,
        profile.enabled_mask);
    TEST_ASSERT_EQUAL_HEX32(0U, profile.battery_switch_mask);
    TEST_ASSERT_EQUAL_HEX32(0U, profile.adc_mode_mask);
    TEST_ASSERT_EQUAL(
        TIC12400_PROFILE_OK,
        TIC12400_Profile_Validate(
            &profile,
            TIC12400_PROFILE_CARRIER_FITTED_MASK));
}

void test_profile_rejects_unfitted_in12(void)
{
    profile.enabled_mask |= 1UL << 12;

    TEST_ASSERT_EQUAL(
        TIC12400_PROFILE_INPUT_NOT_FITTED,
        TIC12400_Profile_Validate(
            &profile,
            TIC12400_PROFILE_CARRIER_FITTED_MASK));
}

void test_profile_allows_battery_switches_only_on_in0_to_in9(void)
{
    profile.battery_switch_mask = (1UL << 0) | (1UL << 9);
    TEST_ASSERT_EQUAL(
        TIC12400_PROFILE_OK,
        TIC12400_Profile_Validate(
            &profile,
            TIC12400_PROFILE_CARRIER_FITTED_MASK));

    profile.battery_switch_mask |= 1UL << 10;
    TEST_ASSERT_EQUAL(
        TIC12400_PROFILE_BATTERY_INPUT_UNSUPPORTED,
        TIC12400_Profile_Validate(
            &profile,
            TIC12400_PROFILE_CARRIER_FITTED_MASK));
}

void test_profile_rejects_modes_on_disabled_inputs(void)
{
    profile.enabled_mask &= ~(uint32_t)(1UL << 4U);
    profile.adc_mode_mask = 1UL << 4;

    TEST_ASSERT_EQUAL(
        TIC12400_PROFILE_MODE_INPUT_DISABLED,
        TIC12400_Profile_Validate(
            &profile,
            TIC12400_PROFILE_CARRIER_FITTED_MASK));
}

void test_comparator_decode_obeys_source_and_sink_polarity(void)
{
    uint32_t above_mask;
    uint32_t closed_mask;

    profile.enabled_mask = (1UL << 0) | (1UL << 1);
    profile.battery_switch_mask = 1UL << 1;
    above_mask = 1UL << 1;

    closed_mask = TIC12400_Profile_DecodeComparatorClosed(
        &profile,
        above_mask);

    TEST_ASSERT_BITS(1UL << 0, 1UL << 0, closed_mask);
    TEST_ASSERT_BITS(1UL << 1, 1UL << 1, closed_mask);

    above_mask = 1UL << 0;
    closed_mask = TIC12400_Profile_DecodeComparatorClosed(
        &profile,
        above_mask);
    TEST_ASSERT_EQUAL_HEX32(0U, closed_mask);
}

void test_profile_helpers_reject_null(void)
{
    TEST_ASSERT_EQUAL(
        TIC12400_PROFILE_INVALID_ARGUMENT,
        TIC12400_Profile_Validate(
            NULL,
            TIC12400_PROFILE_CARRIER_FITTED_MASK));
    TEST_ASSERT_EQUAL_HEX32(
        0U,
        TIC12400_Profile_DecodeComparatorClosed(NULL, 0U));
}

void test_profile_builds_both_edge_comparator_interrupt_masks(void)
{
    TEST_ASSERT_EQUAL_HEX32(
        0x00FFFFFFUL,
        TIC12400_Profile_BuildComparatorInterruptEnable(
            &profile,
            0U));
    TEST_ASSERT_EQUAL_HEX32(
        0x00FFFFFCUL,
        TIC12400_Profile_BuildComparatorInterruptEnable(
            &profile,
            12U));

    profile.adc_mode_mask = 1UL << 1;
    TEST_ASSERT_EQUAL_HEX32(
        0x00FFFFF3UL,
        TIC12400_Profile_BuildComparatorInterruptEnable(
            &profile,
            0U));
    TEST_ASSERT_EQUAL_HEX32(
        0U,
        TIC12400_Profile_BuildComparatorInterruptEnable(
            &profile,
            1U));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(
        test_carrier_profile_uses_fitted_ground_comparator_inputs);
    RUN_TEST(test_profile_rejects_unfitted_in12);
    RUN_TEST(
        test_profile_allows_battery_switches_only_on_in0_to_in9);
    RUN_TEST(test_profile_rejects_modes_on_disabled_inputs);
    RUN_TEST(test_comparator_decode_obeys_source_and_sink_polarity);
    RUN_TEST(test_profile_helpers_reject_null);
    RUN_TEST(
        test_profile_builds_both_edge_comparator_interrupt_masks);
    return UNITY_END();
}
