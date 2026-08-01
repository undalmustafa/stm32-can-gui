#ifndef MAIN_H
#define MAIN_H

#include "stm32h7xx_hal_stub.h"

typedef enum
{
    LED_GREEN = 0,
    LED_YELLOW,
    LED_RED
} Led_TypeDef;

typedef enum
{
    BUTTON_USER = 0U
} Button_TypeDef;

extern GPIO_TypeDef test_gpio_a;
extern GPIO_TypeDef test_gpio_g;

#define GPIOA (&test_gpio_a)
#define GPIOG (&test_gpio_g)
#define GPIO_PIN_2 0x0004U
#define GPIO_PIN_4 0x0010U
#define GPIO_PIN_6 0x0040U
#define EXTI9_5_IRQn ((IRQn_Type)23)

#define TIC12400_RESET_Pin       GPIO_PIN_2
#define TIC12400_RESET_GPIO_Port GPIOA
#define TIC12400_CS_Pin          GPIO_PIN_4
#define TIC12400_CS_GPIO_Port    GPIOA
#define TIC12400_INT_Pin         GPIO_PIN_6
#define TIC12400_INT_GPIO_Port   GPIOG
#define TIC12400_INT_EXTI_IRQn   EXTI9_5_IRQn

void BSP_LED_On(Led_TypeDef led);
void BSP_LED_Off(Led_TypeDef led);

#endif /* MAIN_H */
