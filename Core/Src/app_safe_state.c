#include "app_safe_state.h"

#include "main.h"

void App_SafeState_Engage(void)
{
    /*
     * PA0 is the TIM2_CH1 PWM output on NUCLEO-H7A3ZI-Q. Do not depend on
     * HAL state here: this function is also used after corrupted init paths
     * and from Cortex fault handlers.
     */
    SET_BIT(RCC->AHB4ENR, RCC_AHB4ENR_GPIOAEN);
    SET_BIT(RCC->APB1LENR, RCC_APB1LENR_TIM2EN);
    (void)RCC->AHB4ENR;
    (void)RCC->APB1LENR;
    __DSB();

    CLEAR_BIT(TIM2->CCER, TIM_CCER_CC1E);
    CLEAR_BIT(TIM2->CR1, TIM_CR1_CEN);

    GPIOA->BSRR = GPIO_BSRR_BR0;
    CLEAR_BIT(GPIOA->OTYPER, GPIO_OTYPER_OT0);
    MODIFY_REG(GPIOA->PUPDR, GPIO_PUPDR_PUPD0_Msk, 0U);
    MODIFY_REG(GPIOA->MODER,
               GPIO_MODER_MODE0_Msk,
               GPIO_MODER_MODE0_0);
    __DSB();
}
