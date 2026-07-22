#ifndef APP_PWM_H
#define APP_PWM_H

#include "stm32h7xx_hal.h"

#include <stdint.h>

#define APP_PWM_TIMER_CLOCK_HZ       64000000UL
#define APP_PWM_MIN_FREQUENCY_HZ     1UL
#define APP_PWM_MAX_FREQUENCY_HZ     1000000UL
#define APP_PWM_MAX_DUTY_PERMILLE    1000U

typedef enum
{
    APP_PWM_RESULT_OK = 0,
    APP_PWM_RESULT_INVALID_ARGUMENT,
    APP_PWM_RESULT_TIMER_ERROR
} App_PWM_Result_t;

typedef struct
{
    uint32_t requested_frequency_hz;
    uint32_t actual_frequency_hz;
    uint32_t period_counts;
    uint16_t duty_permille;
    uint16_t prescaler;
    App_PWM_Result_t result;
    uint8_t initialized;
    uint8_t enabled;
} App_PWM_Status_t;

HAL_StatusTypeDef App_PWM_Init(TIM_HandleTypeDef *timer);
App_PWM_Result_t App_PWM_Configure(uint8_t enabled,
                                  uint32_t frequency_hz,
                                  uint16_t duty_permille);
void App_PWM_GetStatus(App_PWM_Status_t *status);

#endif /* APP_PWM_H */
