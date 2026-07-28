#include "unity.h"

#include "app_control_policy.h"

void setUp(void)
{
    App_ControlPolicy_Init();
}

void tearDown(void)
{
}

static App_ControlPolicySnapshot_t Snapshot(void)
{
    return App_ControlPolicy_GetSnapshot();
}

void test_invalid_switch_data_forces_controlled_resources_safe(void)
{
    App_ControlPolicy_SetLedRequest(1U, 1U);
    App_ControlPolicy_SetPwmRequest(1U);
    App_ControlPolicy_SetSlotRequest(1U, 1U);
    App_ControlPolicy_SetSlotRequest(2U, 1U);
    App_ControlPolicy_UpdateSwitches(0U, 0x0FUL);

    App_ControlPolicySnapshot_t state = Snapshot();
    TEST_ASSERT_EQUAL_UINT8(0U, state.led_1_effective);
    TEST_ASSERT_EQUAL_UINT8(0U, state.pwm_effective);
    TEST_ASSERT_EQUAL_UINT8(0U, state.slot_1_effective);
    TEST_ASSERT_EQUAL_UINT8(0U, state.slot_2_effective);
    TEST_ASSERT_EQUAL_UINT8(1U, state.pwm_blocked);
    TEST_ASSERT_EQUAL_UINT8(1U, state.slot_1_blocked);
    TEST_ASSERT_EQUAL_UINT8(1U, state.slot_2_blocked);
}

void test_in0_directly_controls_led1_and_overrides_gui(void)
{
    App_ControlPolicy_SetLedRequest(1U, 0U);
    App_ControlPolicy_UpdateSwitches(
        1U,
        1UL << APP_CONTROL_POLICY_LED1_INPUT);

    App_ControlPolicySnapshot_t state = Snapshot();
    TEST_ASSERT_EQUAL_UINT8(1U, state.led_1_effective);
    TEST_ASSERT_EQUAL_UINT8(1U, state.led_1_overridden);

    App_ControlPolicy_SetLedRequest(1U, 1U);
    state = Snapshot();
    TEST_ASSERT_EQUAL_UINT8(1U, state.led_1_effective);
    TEST_ASSERT_EQUAL_UINT8(0U, state.led_1_overridden);

    App_ControlPolicy_UpdateSwitches(1U, 0U);
    state = Snapshot();
    TEST_ASSERT_EQUAL_UINT8(0U, state.led_1_effective);
    TEST_ASSERT_EQUAL_UINT8(1U, state.led_1_overridden);
}

void test_led2_remains_under_gui_control(void)
{
    App_ControlPolicy_UpdateSwitches(0U, 0U);
    App_ControlPolicy_SetLedRequest(2U, 1U);
    TEST_ASSERT_EQUAL_UINT8(1U, Snapshot().led_2_effective);

    App_ControlPolicy_SetLedRequest(2U, 0U);
    TEST_ASSERT_EQUAL_UINT8(0U, Snapshot().led_2_effective);
}

void test_in1_is_pwm_permission_not_force_on(void)
{
    App_ControlPolicy_UpdateSwitches(
        1U,
        1UL << APP_CONTROL_POLICY_PWM_INPUT);

    App_ControlPolicySnapshot_t state = Snapshot();
    TEST_ASSERT_EQUAL_UINT8(1U, state.pwm_permitted);
    TEST_ASSERT_EQUAL_UINT8(0U, state.pwm_effective);

    App_ControlPolicy_SetPwmRequest(1U);
    state = Snapshot();
    TEST_ASSERT_EQUAL_UINT8(1U, state.pwm_effective);
    TEST_ASSERT_EQUAL_UINT8(0U, state.pwm_blocked);

    App_ControlPolicy_UpdateSwitches(1U, 0U);
    state = Snapshot();
    TEST_ASSERT_EQUAL_UINT8(0U, state.pwm_effective);
    TEST_ASSERT_EQUAL_UINT8(1U, state.pwm_blocked);
}

void test_in2_and_in3_independently_gate_can_slots(void)
{
    App_ControlPolicy_SetSlotRequest(1U, 1U);
    App_ControlPolicy_SetSlotRequest(2U, 1U);
    App_ControlPolicy_UpdateSwitches(
        1U,
        (1UL << APP_CONTROL_POLICY_SLOT1_INPUT) |
        (1UL << APP_CONTROL_POLICY_SLOT2_INPUT));

    App_ControlPolicySnapshot_t state = Snapshot();
    TEST_ASSERT_EQUAL_UINT8(1U, state.slot_1_effective);
    TEST_ASSERT_EQUAL_UINT8(1U, state.slot_2_effective);

    App_ControlPolicy_UpdateSwitches(
        1U,
        1UL << APP_CONTROL_POLICY_SLOT2_INPUT);
    state = Snapshot();
    TEST_ASSERT_EQUAL_UINT8(0U, state.slot_1_effective);
    TEST_ASSERT_EQUAL_UINT8(1U, state.slot_2_effective);
    TEST_ASSERT_EQUAL_UINT8(1U, state.slot_1_blocked);
    TEST_ASSERT_EQUAL_UINT8(0U, state.slot_2_blocked);
}

void test_invalid_resource_numbers_are_ignored(void)
{
    uint32_t updates_before = Snapshot().update_count;

    App_ControlPolicy_SetLedRequest(3U, 1U);
    App_ControlPolicy_SetSlotRequest(3U, 1U);

    App_ControlPolicySnapshot_t state = Snapshot();
    TEST_ASSERT_EQUAL_UINT32(updates_before, state.update_count);
    TEST_ASSERT_EQUAL_UINT8(0U, state.led_1_requested);
    TEST_ASSERT_EQUAL_UINT8(0U, state.led_2_requested);
    TEST_ASSERT_EQUAL_UINT8(0U, state.slot_1_requested);
    TEST_ASSERT_EQUAL_UINT8(0U, state.slot_2_requested);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_invalid_switch_data_forces_controlled_resources_safe);
    RUN_TEST(test_in0_directly_controls_led1_and_overrides_gui);
    RUN_TEST(test_led2_remains_under_gui_control);
    RUN_TEST(test_in1_is_pwm_permission_not_force_on);
    RUN_TEST(test_in2_and_in3_independently_gate_can_slots);
    RUN_TEST(test_invalid_resource_numbers_are_ignored);
    return UNITY_END();
}
