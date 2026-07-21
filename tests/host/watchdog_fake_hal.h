#ifndef WATCHDOG_FAKE_HAL_H
#define WATCHDOG_FAKE_HAL_H

#include <stdint.h>

#define __MAIN_H
#define STM32H7xx_HAL_IWDG_H
#define HAL_IWDG_MODULE_ENABLED

typedef enum
{
    HAL_OK = 0x00U,
    HAL_ERROR = 0x01U
} HAL_StatusTypeDef;

typedef struct
{
    uint32_t Prescaler;
    uint32_t Reload;
    uint32_t Window;
} IWDG_InitTypeDef;

typedef struct
{
    void *Instance;
    IWDG_InitTypeDef Init;
} IWDG_HandleTypeDef;

extern uint32_t g_fakeIwdgInstance;

#define IWDG1 ((void *)&g_fakeIwdgInstance)
#define IWDG_PRESCALER_128 0x05U
#define IWDG_WINDOW_DISABLE 0x0FFFU

uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_IWDG_Init(IWDG_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *handle);

#endif /* WATCHDOG_FAKE_HAL_H */
