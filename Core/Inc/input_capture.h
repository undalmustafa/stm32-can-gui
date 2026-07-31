#ifndef INPUT_CAPTURE_H
#define INPUT_CAPTURE_H

#include "stm32h7xx_hal.h"

#include <stdint.h>

typedef enum
{
    INPUT_CAPTURE_OK = 0,
    INPUT_CAPTURE_ERROR_INVALID_ARGUMENT,
    INPUT_CAPTURE_ERROR_HAL
} Input_Capture_Result_t;

typedef struct
{
    uint8_t initialized;
    uint8_t signal_detected;
    uint32_t frequency_hz;
    uint8_t duty_percent;
    uint32_t period_ticks;
    uint32_t pulse_ticks;
    uint32_t edge_count;
    uint32_t overflow_count;
    uint32_t counter_clock_hz;
} Input_Capture_State_t;

Input_Capture_Result_t Input_Capture_Init(
    TIM_HandleTypeDef *htim,
    uint32_t counter_clock_hz);
Input_Capture_State_t Input_Capture_GetState(void);
void Input_Capture_Process(void);

#endif /* INPUT_CAPTURE_H */
