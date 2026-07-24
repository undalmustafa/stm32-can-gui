#include "input_capture.h"

#include <stddef.h>

static TIM_HandleTypeDef *capture_timer;
static volatile Input_Capture_State_t capture_state;

Input_Capture_Result_t Input_Capture_Init(
    TIM_HandleTypeDef *htim,
    uint32_t counter_clock_hz)
{
    if ((htim == NULL) || (counter_clock_hz == 0U))
    {
        return INPUT_CAPTURE_ERROR_INVALID_ARGUMENT;
    }

    capture_timer = htim;
    capture_state.counter_clock_hz = counter_clock_hz;

    /*
     * In reset slave mode each rising edge resets the counter. Restrict update
     * interrupt requests to genuine counter overflow/underflow events so a
     * valid capture is not immediately reported as signal loss.
     */
    __HAL_TIM_URS_ENABLE(htim);
    __HAL_TIM_CLEAR_FLAG(htim, TIM_FLAG_UPDATE);

    if (HAL_TIM_IC_Start_IT(htim, TIM_CHANNEL_1) != HAL_OK)
    {
        return INPUT_CAPTURE_ERROR_HAL;
    }

    if (HAL_TIM_IC_Start(htim, TIM_CHANNEL_2) != HAL_OK)
    {
        (void)HAL_TIM_IC_Stop_IT(htim, TIM_CHANNEL_1);
        return INPUT_CAPTURE_ERROR_HAL;
    }

    __HAL_TIM_ENABLE_IT(htim, TIM_IT_UPDATE);
    return INPUT_CAPTURE_OK;
}

void Input_Capture_IRQHandler(TIM_HandleTypeDef *htim)
{
    uint32_t period;
    uint32_t pulse;

    if ((htim != capture_timer) ||
        (htim->Channel != HAL_TIM_ACTIVE_CHANNEL_1))
    {
        return;
    }

    period = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
    pulse = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2);

    if ((period == 0U) || (pulse > period))
    {
        return;
    }

    capture_state.period_ticks = period;
    capture_state.pulse_ticks = pulse;
    capture_state.frequency_hz =
        capture_state.counter_clock_hz / period;
    capture_state.duty_percent =
        (uint8_t)(((uint64_t)pulse * 100U + (period / 2U)) / period);
    capture_state.edge_count++;
    capture_state.signal_detected = 1U;
}

void Input_Capture_OverflowHandler(TIM_HandleTypeDef *htim)
{
    if (htim != capture_timer)
    {
        return;
    }

    capture_state.overflow_count++;
    capture_state.signal_detected = 0U;
    capture_state.frequency_hz = 0U;
    capture_state.duty_percent = 0U;
}

Input_Capture_State_t Input_Capture_GetState(void)
{
    Input_Capture_State_t snapshot;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    snapshot = (Input_Capture_State_t)capture_state;
    if (primask == 0U)
    {
        __enable_irq();
    }

    return snapshot;
}
