#include "app_control_policy.h"

volatile App_ControlPolicySnapshot_t g_appControlPolicy;

static uint8_t App_ControlPolicy_IsClosed(uint8_t input)
{
    if (g_appControlPolicy.switch_data_valid == 0U)
    {
        return 0U;
    }

    return ((g_appControlPolicy.closed_switch_mask &
             (1UL << input)) != 0U) ? 1U : 0U;
}

static void App_ControlPolicy_Recalculate(void)
{
    uint8_t led_1_override =
        App_ControlPolicy_IsClosed(
            APP_CONTROL_POLICY_LED1_INPUT);
    uint8_t pwm_inhibited =
        App_ControlPolicy_IsClosed(
            APP_CONTROL_POLICY_PWM_INPUT);
    uint8_t slot_1_inhibited =
        App_ControlPolicy_IsClosed(
            APP_CONTROL_POLICY_SLOT1_INPUT);
    uint8_t slot_2_inhibited =
        App_ControlPolicy_IsClosed(
            APP_CONTROL_POLICY_SLOT2_INPUT);

    /*
     * Remote requests own normal operation. A valid closed TIC input is an
     * active local override: IN0 forces LED1 on, while IN1-IN3 inhibit their
     * mapped functions. Invalid TIC data remains fail-safe for PWM and the
     * configurable CAN slots, but does not take GUI ownership away from LEDs.
     */
    g_appControlPolicy.led_1_effective =
        ((g_appControlPolicy.led_1_requested != 0U) ||
         (led_1_override != 0U)) ? 1U : 0U;
    g_appControlPolicy.led_2_effective =
        g_appControlPolicy.led_2_requested;

    g_appControlPolicy.pwm_permitted =
        ((g_appControlPolicy.switch_data_valid != 0U) &&
         (pwm_inhibited == 0U)) ? 1U : 0U;
    g_appControlPolicy.slot_1_permitted =
        ((g_appControlPolicy.switch_data_valid != 0U) &&
         (slot_1_inhibited == 0U)) ? 1U : 0U;
    g_appControlPolicy.slot_2_permitted =
        ((g_appControlPolicy.switch_data_valid != 0U) &&
         (slot_2_inhibited == 0U)) ? 1U : 0U;

    g_appControlPolicy.pwm_effective =
        ((g_appControlPolicy.pwm_requested != 0U) &&
         (g_appControlPolicy.pwm_permitted != 0U)) ? 1U : 0U;
    g_appControlPolicy.slot_1_effective =
        ((g_appControlPolicy.slot_1_requested != 0U) &&
         (g_appControlPolicy.slot_1_permitted != 0U)) ? 1U : 0U;
    g_appControlPolicy.slot_2_effective =
        ((g_appControlPolicy.slot_2_requested != 0U) &&
         (g_appControlPolicy.slot_2_permitted != 0U)) ? 1U : 0U;

    g_appControlPolicy.led_1_overridden =
        ((led_1_override != 0U) &&
         (g_appControlPolicy.led_1_requested == 0U)) ? 1U : 0U;
    g_appControlPolicy.pwm_blocked =
        ((g_appControlPolicy.pwm_requested != 0U) &&
         (g_appControlPolicy.pwm_permitted == 0U)) ? 1U : 0U;
    g_appControlPolicy.slot_1_blocked =
        ((g_appControlPolicy.slot_1_requested != 0U) &&
         (g_appControlPolicy.slot_1_permitted == 0U)) ? 1U : 0U;
    g_appControlPolicy.slot_2_blocked =
        ((g_appControlPolicy.slot_2_requested != 0U) &&
         (g_appControlPolicy.slot_2_permitted == 0U)) ? 1U : 0U;
}

void App_ControlPolicy_Init(void)
{
    g_appControlPolicy = (App_ControlPolicySnapshot_t){0};
}

void App_ControlPolicy_UpdateSwitches(
    uint8_t data_valid,
    uint32_t closed_switch_mask)
{
    uint8_t normalized_valid = (data_valid != 0U) ? 1U : 0U;
    uint32_t normalized_mask =
        (normalized_valid != 0U) ? closed_switch_mask : 0U;

    if ((g_appControlPolicy.switch_data_valid != normalized_valid) ||
        (g_appControlPolicy.closed_switch_mask != normalized_mask))
    {
        g_appControlPolicy.switch_data_valid = normalized_valid;
        g_appControlPolicy.closed_switch_mask = normalized_mask;
        g_appControlPolicy.update_count++;
    }
    App_ControlPolicy_Recalculate();
}

void App_ControlPolicy_SetLedRequest(
    uint8_t led_number,
    uint8_t requested_on)
{
    if (led_number == 1U)
    {
        uint8_t normalized = (requested_on != 0U) ? 1U : 0U;
        if (g_appControlPolicy.led_1_requested != normalized)
        {
            g_appControlPolicy.led_1_requested = normalized;
            g_appControlPolicy.update_count++;
        }
    }
    else if (led_number == 2U)
    {
        uint8_t normalized = (requested_on != 0U) ? 1U : 0U;
        if (g_appControlPolicy.led_2_requested != normalized)
        {
            g_appControlPolicy.led_2_requested = normalized;
            g_appControlPolicy.update_count++;
        }
    }
    else
    {
        return;
    }

    App_ControlPolicy_Recalculate();
}

void App_ControlPolicy_SetPwmRequest(uint8_t requested_on)
{
    uint8_t normalized = (requested_on != 0U) ? 1U : 0U;
    if (g_appControlPolicy.pwm_requested != normalized)
    {
        g_appControlPolicy.pwm_requested = normalized;
        g_appControlPolicy.update_count++;
    }
    App_ControlPolicy_Recalculate();
}

void App_ControlPolicy_SetSlotRequest(
    uint8_t slot_number,
    uint8_t requested_on)
{
    if (slot_number == 1U)
    {
        uint8_t normalized = (requested_on != 0U) ? 1U : 0U;
        if (g_appControlPolicy.slot_1_requested != normalized)
        {
            g_appControlPolicy.slot_1_requested = normalized;
            g_appControlPolicy.update_count++;
        }
    }
    else if (slot_number == 2U)
    {
        uint8_t normalized = (requested_on != 0U) ? 1U : 0U;
        if (g_appControlPolicy.slot_2_requested != normalized)
        {
            g_appControlPolicy.slot_2_requested = normalized;
            g_appControlPolicy.update_count++;
        }
    }
    else
    {
        return;
    }

    App_ControlPolicy_Recalculate();
}

App_ControlPolicySnapshot_t App_ControlPolicy_GetSnapshot(void)
{
    return (App_ControlPolicySnapshot_t)g_appControlPolicy;
}
