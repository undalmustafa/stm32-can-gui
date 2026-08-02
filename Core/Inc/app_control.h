#ifndef APP_CONTROL_H
#define APP_CONTROL_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: none
 */

#include <stdint.h>

typedef struct
{
    uint32_t process_count;
    uint32_t output_change_count;
    uint32_t self_test_cancel_count;
    uint8_t led_1_on;
    uint8_t led_2_on;
    uint8_t pwm_running;
} App_ControlSnapshot_t;

/* Convenient for STM32CubeIDE Live Expressions. */
extern volatile App_ControlSnapshot_t g_appControl;

void App_Control_Init(void);
void App_Control_Process(void);
App_ControlSnapshot_t App_Control_GetSnapshot(void);

#endif /* APP_CONTROL_H */
