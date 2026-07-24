#ifndef TEST_PWM_CONTROL_H
#define TEST_PWM_CONTROL_H

#include <stdint.h>

typedef enum
{
    PWM_CONTROL_OK = 0,
    PWM_CONTROL_ERROR
} PWM_Control_Result_t;

typedef struct
{
    uint8_t initialized;
    uint8_t running;
    uint8_t duty_percent;
    uint32_t requested_frequency_hz;
    uint32_t actual_frequency_hz;
    uint32_t counter_clock_hz;
    uint32_t period_ticks;
    uint32_t pulse_ticks;
    PWM_Control_Result_t last_result;
} PWM_Control_State_t;

PWM_Control_Result_t PWM_Control_Set(uint32_t frequency_hz,
                                     uint8_t duty_percent);
PWM_Control_Result_t PWM_Control_Stop(void);
PWM_Control_Result_t PWM_Control_Start(void);
PWM_Control_State_t PWM_Control_GetState(void);

#endif
