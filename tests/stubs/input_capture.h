#ifndef TEST_INPUT_CAPTURE_H
#define TEST_INPUT_CAPTURE_H

#include <stdint.h>

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

Input_Capture_State_t Input_Capture_GetState(void);

#endif
