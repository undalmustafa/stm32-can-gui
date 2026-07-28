#ifndef MAIN_H
#define MAIN_H

#include "stm32h7xx_hal_stub.h"

typedef enum
{
    LED_GREEN = 0,
    LED_YELLOW,
    LED_RED
} Led_TypeDef;

void BSP_LED_On(Led_TypeDef led);
void BSP_LED_Off(Led_TypeDef led);

#endif /* MAIN_H */
