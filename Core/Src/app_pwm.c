#include "app_pwm.h"

#include <stddef.h>

#define APP_PWM_TIMER_CHANNEL       TIM_CHANNEL_4
#define APP_PWM_COUNTER_CAPACITY    65536UL

static TIM_HandleTypeDef *pwm_timer;
static App_PWM_Status_t pwm_status;

static uint32_t App_PWM_DivideRounded(uint64_t numerator,
                                      uint64_t denominator)
{
    return (uint32_t)((numerator + (denominator / 2ULL)) / denominator);
}

HAL_StatusTypeDef App_PWM_Init(TIM_HandleTypeDef *timer)
{
    if (timer == NULL)
    {
        return HAL_ERROR;
    }

    pwm_timer = timer;
    pwm_status = (App_PWM_Status_t){0};
    pwm_status.initialized = 1U;
    pwm_status.requested_frequency_hz = 1000U;
    pwm_status.actual_frequency_hz = 1000U;
    pwm_status.period_counts = 1000U;
    pwm_status.duty_permille = 500U;
    pwm_status.prescaler = 64U;
    pwm_status.result = APP_PWM_RESULT_OK;

    return HAL_OK;
}

App_PWM_Result_t App_PWM_Configure(uint8_t enabled,
                                  uint32_t frequency_hz,
                                  uint16_t duty_permille)
{
    uint32_t prescaler_divisor;
    uint32_t period_counts;
    uint32_t pulse_counts;
    uint64_t minimum_divisor;

    if ((pwm_timer == NULL) ||
        (pwm_status.initialized == 0U) ||
        (frequency_hz < APP_PWM_MIN_FREQUENCY_HZ) ||
        (frequency_hz > APP_PWM_MAX_FREQUENCY_HZ) ||
        (duty_permille > APP_PWM_MAX_DUTY_PERMILLE))
    {
        pwm_status.result = APP_PWM_RESULT_INVALID_ARGUMENT;
        return pwm_status.result;
    }

    minimum_divisor = (uint64_t)frequency_hz * APP_PWM_COUNTER_CAPACITY;
    prescaler_divisor = (uint32_t)(
        ((uint64_t)APP_PWM_TIMER_CLOCK_HZ + minimum_divisor - 1ULL) /
        minimum_divisor);

    if (prescaler_divisor == 0U)
    {
        prescaler_divisor = 1U;
    }

    period_counts = App_PWM_DivideRounded(
        APP_PWM_TIMER_CLOCK_HZ,
        (uint64_t)prescaler_divisor * frequency_hz);

    if (period_counts < 2U)
    {
        period_counts = 2U;
    }
    else if (period_counts > APP_PWM_COUNTER_CAPACITY)
    {
        period_counts = APP_PWM_COUNTER_CAPACITY;
    }

    pulse_counts = App_PWM_DivideRounded(
        (uint64_t)period_counts * duty_permille,
        APP_PWM_MAX_DUTY_PERMILLE);

    if (pwm_status.enabled != 0U)
    {
        if (HAL_TIM_PWM_Stop(pwm_timer, APP_PWM_TIMER_CHANNEL) != HAL_OK)
        {
            pwm_status.result = APP_PWM_RESULT_TIMER_ERROR;
            return pwm_status.result;
        }
    }

    __HAL_TIM_SET_PRESCALER(pwm_timer, prescaler_divisor - 1U);
    __HAL_TIM_SET_AUTORELOAD(pwm_timer, period_counts - 1U);
    __HAL_TIM_SET_COMPARE(pwm_timer, APP_PWM_TIMER_CHANNEL, pulse_counts);
    __HAL_TIM_SET_COUNTER(pwm_timer, 0U);
    if (HAL_TIM_GenerateEvent(pwm_timer, TIM_EVENTSOURCE_UPDATE) != HAL_OK)
    {
        pwm_status.enabled = 0U;
        pwm_status.result = APP_PWM_RESULT_TIMER_ERROR;
        return pwm_status.result;
    }

    pwm_status.requested_frequency_hz = frequency_hz;
    pwm_status.actual_frequency_hz = App_PWM_DivideRounded(
        APP_PWM_TIMER_CLOCK_HZ,
        (uint64_t)prescaler_divisor * period_counts);
    pwm_status.period_counts = period_counts;
    pwm_status.duty_permille = duty_permille;
    pwm_status.prescaler = (uint16_t)prescaler_divisor;
    pwm_status.enabled = 0U;
    pwm_status.result = APP_PWM_RESULT_OK;

    if (enabled != 0U)
    {
        if (HAL_TIM_PWM_Start(pwm_timer, APP_PWM_TIMER_CHANNEL) != HAL_OK)
        {
            pwm_status.result = APP_PWM_RESULT_TIMER_ERROR;
            return pwm_status.result;
        }

        pwm_status.enabled = 1U;
    }

    return pwm_status.result;
}

void App_PWM_GetStatus(App_PWM_Status_t *status)
{
    if (status != NULL)
    {
        *status = pwm_status;
    }
}
