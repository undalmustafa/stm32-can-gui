#include "pwm_control.h"

#include <stddef.h>

#define PWM_CONTROL_MIN_FREQUENCY_HZ  1UL
#define PWM_CONTROL_MAX_FREQUENCY_HZ  1000000UL

volatile PWM_Control_State_t g_pwmControlState;

static TIM_HandleTypeDef *pwm_timer;
static uint32_t pwm_channel;

static PWM_Control_Result_t PWM_Control_RecordResult(
    PWM_Control_Result_t result)
{
    g_pwmControlState.last_result = result;
    return result;
}

PWM_Control_Result_t PWM_Control_Init(
    TIM_HandleTypeDef *timer_handle,
    uint32_t channel,
    uint32_t counter_clock_hz)
{
    if ((timer_handle == NULL) || (counter_clock_hz == 0U))
    {
        return PWM_Control_RecordResult(
            PWM_CONTROL_ERROR_NOT_INITIALIZED);
    }

    pwm_timer = timer_handle;
    pwm_channel = channel;

    g_pwmControlState.counter_clock_hz = counter_clock_hz;

    if (HAL_TIM_PWM_Start(pwm_timer, pwm_channel) != HAL_OK)
    {
        g_pwmControlState.running = 0U;

        return PWM_Control_RecordResult(
            PWM_CONTROL_ERROR_HAL);
    }

    g_pwmControlState.initialized = 1U;
    g_pwmControlState.running = 1U;

    return PWM_Control_RecordResult(PWM_CONTROL_OK);
}

PWM_Control_Result_t PWM_Control_Set(
    uint32_t frequency_hz,
    uint8_t duty_percent)
{
    uint32_t period_ticks;
    uint32_t pulse_ticks;

    if ((g_pwmControlState.initialized == 0U) ||
        (pwm_timer == NULL))
    {
        return PWM_Control_RecordResult(
            PWM_CONTROL_ERROR_NOT_INITIALIZED);
    }

    if ((frequency_hz < PWM_CONTROL_MIN_FREQUENCY_HZ) ||
        (frequency_hz > PWM_CONTROL_MAX_FREQUENCY_HZ))
    {
        return PWM_Control_RecordResult(
            PWM_CONTROL_ERROR_INVALID_FREQUENCY);
    }

    if (duty_percent > 100U)
    {
        return PWM_Control_RecordResult(
            PWM_CONTROL_ERROR_INVALID_DUTY);
    }

    /*
     * Round to the nearest whole timer count instead of always
     * truncating downward.
     */
    period_ticks =
        (g_pwmControlState.counter_clock_hz +
         (frequency_hz / 2U)) /
        frequency_hz;

    if (period_ticks < 1U)
    {
        return PWM_Control_RecordResult(
            PWM_CONTROL_ERROR_INVALID_FREQUENCY);
    }

    pulse_ticks =
        (uint32_t)(((uint64_t)period_ticks *
                    duty_percent +
                    50U) /
                   100U);

    /*
     * ARR and CCR1 are preloaded. Both values become active at the
     * following timer update event without stopping the PWM output.
     */
    __HAL_TIM_SET_AUTORELOAD(
        pwm_timer,
        period_ticks - 1U);

    __HAL_TIM_SET_COMPARE(
        pwm_timer,
        pwm_channel,
        pulse_ticks);

    g_pwmControlState.requested_frequency_hz = frequency_hz;
    g_pwmControlState.actual_frequency_hz =
        g_pwmControlState.counter_clock_hz / period_ticks;

    /*
     * Report the quantized duty that is physically produced. At the 1 MHz
     * upper limit there is one timer tick per period, so only 0% or 100% can
     * be represented even though intermediate requests remain valid.
     */
    g_pwmControlState.duty_percent =
        (uint8_t)(((uint64_t)pulse_ticks * 100U +
                   (period_ticks / 2U)) /
                  period_ticks);
    g_pwmControlState.period_ticks = period_ticks;
    g_pwmControlState.pulse_ticks = pulse_ticks;

    return PWM_Control_RecordResult(PWM_CONTROL_OK);
}

PWM_Control_Result_t PWM_Control_Stop(void)
{
    if ((g_pwmControlState.initialized == 0U) ||
        (pwm_timer == NULL))
    {
        return PWM_Control_RecordResult(
            PWM_CONTROL_ERROR_NOT_INITIALIZED);
    }

    if (g_pwmControlState.running == 0U)
    {
        return PWM_Control_RecordResult(PWM_CONTROL_OK);
    }

    if (HAL_TIM_PWM_Stop(pwm_timer, pwm_channel) != HAL_OK)
    {
        return PWM_Control_RecordResult(PWM_CONTROL_ERROR_HAL);
    }

    g_pwmControlState.running = 0U;

    return PWM_Control_RecordResult(PWM_CONTROL_OK);
}

PWM_Control_State_t PWM_Control_GetState(void)
{
    return (PWM_Control_State_t)g_pwmControlState;
}

PWM_Control_Result_t PWM_Control_Start(void)
{
    if ((g_pwmControlState.initialized == 0U) ||
        (pwm_timer == NULL))
    {
        return PWM_Control_RecordResult(
            PWM_CONTROL_ERROR_NOT_INITIALIZED);
    }

    if (g_pwmControlState.running != 0U)
    {
        return PWM_Control_RecordResult(PWM_CONTROL_OK);
    }

    if (HAL_TIM_PWM_Start(pwm_timer, pwm_channel) != HAL_OK)
    {
        return PWM_Control_RecordResult(PWM_CONTROL_ERROR_HAL);
    }

    g_pwmControlState.running = 1U;
    return PWM_Control_RecordResult(PWM_CONTROL_OK);
}
