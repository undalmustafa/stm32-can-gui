#include "unity.h"

#include "app_control.h"
#include "app_control_policy.h"
#include "main.h"
#include "pwm_control.h"
#include "pwm_self_test.h"
#include "tic12400_probe.h"

static TIC12400_ProbeSwitchState_t switch_state;
static PWM_Control_State_t pwm_state;
static uint8_t self_test_running;
static uint32_t self_test_cancel_count;
static uint32_t self_test_process_count;
static uint8_t led_state[3];

void BSP_LED_On(Led_TypeDef led)
{
    led_state[led] = 1U;
}

void BSP_LED_Off(Led_TypeDef led)
{
    led_state[led] = 0U;
}

uint8_t TIC12400_Probe_GetSwitchState(
    TIC12400_ProbeSwitchState_t *state)
{
    *state = switch_state;
    return state->data_valid;
}

PWM_Control_Result_t PWM_Control_Start(void)
{
    pwm_state.running = 1U;
    return PWM_CONTROL_OK;
}

PWM_Control_Result_t PWM_Control_Stop(void)
{
    pwm_state.running = 0U;
    return PWM_CONTROL_OK;
}

PWM_Control_State_t PWM_Control_GetState(void)
{
    return pwm_state;
}

uint8_t PWM_SelfTest_IsRunning(void)
{
    return self_test_running;
}

void PWM_SelfTest_Cancel(void)
{
    self_test_running = 0U;
    self_test_cancel_count++;
}

void PWM_SelfTest_Process(void)
{
    self_test_process_count++;
}

void setUp(void)
{
    switch_state = (TIC12400_ProbeSwitchState_t){0};
    pwm_state = (PWM_Control_State_t){0};
    self_test_running = 0U;
    self_test_cancel_count = 0U;
    self_test_process_count = 0U;
    led_state[LED_GREEN] = 0U;
    led_state[LED_YELLOW] = 0U;
    led_state[LED_RED] = 0U;
    App_Control_Init();
}

void tearDown(void)
{
}

void test_Process_applies_physical_led_and_permitted_pwm_without_can(void)
{
    App_ControlSnapshot_t snapshot;

    switch_state.data_valid = 1U;
    switch_state.closed_bitmap =
        (1UL << APP_CONTROL_POLICY_LED1_INPUT) |
        (1UL << APP_CONTROL_POLICY_PWM_INPUT);
    App_ControlPolicy_SetPwmRequest(1U);

    App_Control_Process();
    snapshot = App_Control_GetSnapshot();

    TEST_ASSERT_EQUAL_UINT8(1U, led_state[LED_GREEN]);
    TEST_ASSERT_EQUAL_UINT8(1U, pwm_state.running);
    TEST_ASSERT_EQUAL_UINT8(1U, snapshot.led_1_on);
    TEST_ASSERT_EQUAL_UINT8(1U, snapshot.pwm_running);
    TEST_ASSERT_EQUAL_UINT32(1U, snapshot.process_count);
}

void test_Invalid_switch_data_forces_mapped_outputs_off(void)
{
    switch_state.data_valid = 1U;
    switch_state.closed_bitmap =
        (1UL << APP_CONTROL_POLICY_LED1_INPUT) |
        (1UL << APP_CONTROL_POLICY_PWM_INPUT);
    App_ControlPolicy_SetPwmRequest(1U);
    App_Control_Process();

    switch_state.data_valid = 0U;
    switch_state.closed_bitmap = 0xFFFFFFFFUL;
    App_Control_Process();

    TEST_ASSERT_EQUAL_UINT8(0U, led_state[LED_GREEN]);
    TEST_ASSERT_EQUAL_UINT8(0U, pwm_state.running);
}

void test_Lost_pwm_permission_cancels_running_self_test(void)
{
    App_ControlSnapshot_t snapshot;

    switch_state.data_valid = 1U;
    switch_state.closed_bitmap =
        (1UL << APP_CONTROL_POLICY_PWM_INPUT);
    self_test_running = 1U;
    pwm_state.running = 1U;

    App_Control_Process();
    TEST_ASSERT_EQUAL_UINT32(0U, self_test_cancel_count);

    switch_state.closed_bitmap = 0U;
    App_Control_Process();
    snapshot = App_Control_GetSnapshot();

    TEST_ASSERT_EQUAL_UINT32(1U, self_test_cancel_count);
    TEST_ASSERT_EQUAL_UINT32(1U, snapshot.self_test_cancel_count);
    TEST_ASSERT_EQUAL_UINT8(0U, pwm_state.running);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(
        test_Process_applies_physical_led_and_permitted_pwm_without_can);
    RUN_TEST(test_Invalid_switch_data_forces_mapped_outputs_off);
    RUN_TEST(test_Lost_pwm_permission_cancels_running_self_test);
    return UNITY_END();
}
