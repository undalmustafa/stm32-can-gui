#include "app_control.h"

#include "app_control_policy.h"
#include "main.h"
#include "pwm_control.h"
#include "pwm_self_test.h"
#include "tic12400_probe.h"

volatile App_ControlSnapshot_t g_appControl;

static void App_Control_ApplyLed(
    uint8_t led_number,
    uint8_t effective_on)
{
    volatile uint8_t *current_state;
    Led_TypeDef led;

    if (led_number == 1U)
    {
        current_state = &g_appControl.led_1_on;
        led = LED_GREEN;
    }
    else
    {
        current_state = &g_appControl.led_2_on;
        led = LED_RED;
    }

    if (*current_state == effective_on)
    {
        return;
    }

    *current_state = effective_on;
    g_appControl.output_change_count++;

    if (effective_on != 0U)
    {
        BSP_LED_On(led);
    }
    else
    {
        BSP_LED_Off(led);
    }
}

static void App_Control_ApplyPwm(
    const App_ControlPolicySnapshot_t *policy)
{
    uint8_t previous_reported_state = g_appControl.pwm_running;

    if (PWM_SelfTest_IsRunning() != 0U)
    {
        if (policy->pwm_permitted == 0U)
        {
            PWM_SelfTest_Cancel();
            g_appControl.self_test_cancel_count++;
            (void)PWM_Control_Stop();
        }
    }
    else if (policy->pwm_effective != 0U)
    {
        (void)PWM_Control_Start();
    }
    else
    {
        (void)PWM_Control_Stop();
    }

    g_appControl.pwm_running = PWM_Control_GetState().running;
    if (g_appControl.pwm_running != previous_reported_state)
    {
        g_appControl.output_change_count++;
    }
}

void App_Control_Init(void)
{
    g_appControl = (App_ControlSnapshot_t){0};
    App_ControlPolicy_Init();
}

void App_Control_Process(void)
{
    TIC12400_ProbeSwitchState_t switches = {0};
    App_ControlPolicySnapshot_t policy;

    (void)TIC12400_Probe_GetSwitchState(&switches);
    App_ControlPolicy_UpdateSwitches(
        switches.data_valid,
        switches.closed_bitmap);

    policy = App_ControlPolicy_GetSnapshot();
    App_Control_ApplyLed(1U, policy.led_1_effective);
    App_Control_ApplyLed(2U, policy.led_2_effective);
    App_Control_ApplyPwm(&policy);

    /*
     * The self-test owns the PWM waveform while it is running. Process it
     * between two policy applications so a completed or cancelled test
     * immediately returns to the effective application state.
     */
    PWM_SelfTest_Process();
    policy = App_ControlPolicy_GetSnapshot();
    App_Control_ApplyPwm(&policy);

    g_appControl.process_count++;
}

App_ControlSnapshot_t App_Control_GetSnapshot(void)
{
    return (App_ControlSnapshot_t)g_appControl;
}
