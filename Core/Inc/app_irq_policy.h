#ifndef APP_IRQ_POLICY_H
#define APP_IRQ_POLICY_H

#include <stdint.h>

/*
 * Cortex-M7 implements lower numeric values as higher interrupt priorities.
 * Priority group 4 assigns all implemented bits to preemption priority; every
 * application interrupt therefore uses subpriority zero.
 *
 * Reserved levels are part of the compatibility contract for future ISRs:
 * safety timer (0), SysTick timeout source (1), FDCAN (2), TIC12400 EXTI (3),
 * application timers (4..14), and human-interface/background interrupts (15).
 */
#define APP_IRQ_PRIORITY_LEVEL_COUNT       16U
#define APP_IRQ_PRIORITY_SAFETY_TIMER       0U
#define APP_IRQ_PRIORITY_SYSTICK            1U
#define APP_IRQ_PRIORITY_FDCAN1             2U
#define APP_IRQ_PRIORITY_TIC12400_EXTI      3U
#define APP_IRQ_PRIORITY_APPLICATION_TIMER  4U
#define APP_IRQ_PRIORITY_USER_BUTTON       15U
#define APP_IRQ_SUBPRIORITY                  0U

#if APP_IRQ_PRIORITY_SAFETY_TIMER >= APP_IRQ_PRIORITY_SYSTICK
#error "Safety timer must preempt the HAL timeout source"
#endif

#if APP_IRQ_PRIORITY_SYSTICK >= APP_IRQ_PRIORITY_FDCAN1
#error "SysTick must preempt FDCAN to preserve HAL timeout progress"
#endif

#if APP_IRQ_PRIORITY_FDCAN1 >= APP_IRQ_PRIORITY_TIC12400_EXTI
#error "FDCAN must preempt the TIC12400 external interrupt"
#endif

#if APP_IRQ_PRIORITY_TIC12400_EXTI >= APP_IRQ_PRIORITY_APPLICATION_TIMER
#error "TIC12400 EXTI must preempt application timers"
#endif

#if APP_IRQ_PRIORITY_APPLICATION_TIMER >= APP_IRQ_PRIORITY_USER_BUTTON
#error "Application timers must preempt the user button"
#endif

#if APP_IRQ_PRIORITY_USER_BUTTON >= APP_IRQ_PRIORITY_LEVEL_COUNT
#error "Interrupt priority exceeds the implemented Cortex-M7 range"
#endif

typedef struct
{
    uint8_t safety_timer;
    uint8_t systick;
    uint8_t fdcan1;
    uint8_t tic12400_exti;
    uint8_t application_timer;
    uint8_t user_button;
} App_IrqPolicy_Config_t;

typedef enum
{
    APP_IRQ_POLICY_VALID = 0U,
    APP_IRQ_POLICY_NULL_CONFIG = (1UL << 0),
    APP_IRQ_POLICY_PRIORITY_OUT_OF_RANGE = (1UL << 1),
    APP_IRQ_POLICY_SAFETY_TIMER_BLOCKED = (1UL << 2),
    APP_IRQ_POLICY_TIMEOUT_SOURCE_BLOCKED = (1UL << 3),
    APP_IRQ_POLICY_SERVICE_ORDER_INVALID = (1UL << 4),
    APP_IRQ_POLICY_BACKGROUND_ORDER_INVALID = (1UL << 5)
} App_IrqPolicy_Result_t;

void App_IrqPolicy_GetDefault(App_IrqPolicy_Config_t *config);
uint32_t App_IrqPolicy_Validate(const App_IrqPolicy_Config_t *config);

#endif /* APP_IRQ_POLICY_H */
