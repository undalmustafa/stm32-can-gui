#include "input_capture.h"

#include <stddef.h>

static TIM_HandleTypeDef *capture_timer;
static volatile Input_Capture_State_t capture_state;
static uint32_t last_capture_time_ms;

#define INPUT_CAPTURE_SIGNAL_TIMEOUT_MS  150U

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

    if (HAL_TIM_IC_Start(htim, TIM_CHANNEL_2) != HAL_OK)
    {
        return INPUT_CAPTURE_ERROR_HAL;
    }

    if (HAL_TIM_IC_Start(htim, TIM_CHANNEL_1) != HAL_OK)
    {
        (void)HAL_TIM_IC_Stop(htim, TIM_CHANNEL_2);
        return INPUT_CAPTURE_ERROR_HAL;
    }

    __HAL_TIM_CLEAR_FLAG(htim,
                         TIM_FLAG_CC1 | TIM_FLAG_CC2 |
                         TIM_FLAG_CC1OF | TIM_FLAG_CC2OF);
    last_capture_time_ms = HAL_GetTick();
    return INPUT_CAPTURE_OK;
}

void Input_Capture_Process(void)
{
    uint32_t period;
    uint32_t pulse;
    uint32_t now;
    uint32_t elapsed_ms;
    uint32_t estimated_edges;

    if (capture_timer == NULL)
    {
        return;
    }

    now = HAL_GetTick();
    if (__HAL_TIM_GET_FLAG(capture_timer, TIM_FLAG_CC1) == RESET)
    {
        if ((capture_state.signal_detected != 0U) &&
            ((now - last_capture_time_ms) >=
             INPUT_CAPTURE_SIGNAL_TIMEOUT_MS))
        {
            capture_state.signal_detected = 0U;
            capture_state.frequency_hz = 0U;
            capture_state.duty_percent = 0U;
            capture_state.overflow_count++;
        }
        return;
    }

    period = HAL_TIM_ReadCapturedValue(capture_timer, TIM_CHANNEL_1);
    pulse = HAL_TIM_ReadCapturedValue(capture_timer, TIM_CHANNEL_2);
    __HAL_TIM_CLEAR_FLAG(capture_timer,
                         TIM_FLAG_CC1 | TIM_FLAG_CC2 |
                         TIM_FLAG_CC1OF | TIM_FLAG_CC2OF);

    if ((period == 0U) || (pulse > period))
    {
        return;
    }

    elapsed_ms = now - last_capture_time_ms;
    capture_state.period_ticks = period;
    capture_state.pulse_ticks = pulse;
    capture_state.frequency_hz =
        capture_state.counter_clock_hz / period;
    capture_state.duty_percent =
        (uint8_t)(((uint64_t)pulse * 100U + (period / 2U)) / period);
    estimated_edges = (uint32_t)(
        ((uint64_t)capture_state.frequency_hz * elapsed_ms + 500U) /
        1000U);
    capture_state.edge_count +=
        (estimated_edges > 0U) ? estimated_edges : 1U;
    capture_state.signal_detected = 1U;
    last_capture_time_ms = now;
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
