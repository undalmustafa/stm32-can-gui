#ifndef APP_CONTROL_POLICY_H
#define APP_CONTROL_POLICY_H

#include <stdint.h>

#define APP_CONTROL_POLICY_LED1_INPUT   0U
#define APP_CONTROL_POLICY_PWM_INPUT    1U
#define APP_CONTROL_POLICY_SLOT1_INPUT  2U
#define APP_CONTROL_POLICY_SLOT2_INPUT  3U

typedef struct
{
    uint32_t closed_switch_mask;
    uint32_t update_count;
    uint8_t switch_data_valid;

    uint8_t led_1_requested;
    uint8_t led_2_requested;
    uint8_t pwm_requested;
    uint8_t slot_1_requested;
    uint8_t slot_2_requested;

    uint8_t led_1_effective;
    uint8_t led_2_effective;
    uint8_t pwm_effective;
    uint8_t slot_1_effective;
    uint8_t slot_2_effective;

    uint8_t pwm_permitted;
    uint8_t slot_1_permitted;
    uint8_t slot_2_permitted;

    uint8_t led_1_overridden;
    uint8_t pwm_blocked;
    uint8_t slot_1_blocked;
    uint8_t slot_2_blocked;
} App_ControlPolicySnapshot_t;

extern volatile App_ControlPolicySnapshot_t g_appControlPolicy;

void App_ControlPolicy_Init(void);

void App_ControlPolicy_UpdateSwitches(
    uint8_t data_valid,
    uint32_t closed_switch_mask);

void App_ControlPolicy_SetLedRequest(
    uint8_t led_number,
    uint8_t requested_on);

void App_ControlPolicy_SetPwmRequest(uint8_t requested_on);

void App_ControlPolicy_SetSlotRequest(
    uint8_t slot_number,
    uint8_t requested_on);

App_ControlPolicySnapshot_t App_ControlPolicy_GetSnapshot(void);

#endif /* APP_CONTROL_POLICY_H */
