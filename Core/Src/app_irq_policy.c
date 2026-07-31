#include "app_irq_policy.h"

#include <stddef.h>

void App_IrqPolicy_GetDefault(App_IrqPolicy_Config_t *config)
{
    if (config == NULL)
    {
        return;
    }

    config->safety_timer = APP_IRQ_PRIORITY_SAFETY_TIMER;
    config->systick = APP_IRQ_PRIORITY_SYSTICK;
    config->fdcan1 = APP_IRQ_PRIORITY_FDCAN1;
    config->tic12400_exti = APP_IRQ_PRIORITY_TIC12400_EXTI;
    config->application_timer = APP_IRQ_PRIORITY_APPLICATION_TIMER;
    config->user_button = APP_IRQ_PRIORITY_USER_BUTTON;
}

static uint8_t App_IrqPolicy_PriorityOutOfRange(
    const App_IrqPolicy_Config_t *config)
{
    return ((config->safety_timer >= APP_IRQ_PRIORITY_LEVEL_COUNT) ||
            (config->systick >= APP_IRQ_PRIORITY_LEVEL_COUNT) ||
            (config->fdcan1 >= APP_IRQ_PRIORITY_LEVEL_COUNT) ||
            (config->tic12400_exti >= APP_IRQ_PRIORITY_LEVEL_COUNT) ||
            (config->application_timer >= APP_IRQ_PRIORITY_LEVEL_COUNT) ||
            (config->user_button >= APP_IRQ_PRIORITY_LEVEL_COUNT)) ? 1U : 0U;
}

uint32_t App_IrqPolicy_Validate(const App_IrqPolicy_Config_t *config)
{
    uint32_t result = APP_IRQ_POLICY_VALID;

    if (config == NULL)
    {
        return APP_IRQ_POLICY_NULL_CONFIG;
    }

    if (App_IrqPolicy_PriorityOutOfRange(config) != 0U)
    {
        result |= APP_IRQ_POLICY_PRIORITY_OUT_OF_RANGE;
    }
    if (config->safety_timer >= config->systick)
    {
        result |= APP_IRQ_POLICY_SAFETY_TIMER_BLOCKED;
    }
    if ((config->systick >= config->fdcan1) ||
        (config->systick >= config->tic12400_exti) ||
        (config->systick >= config->application_timer) ||
        (config->systick >= config->user_button))
    {
        result |= APP_IRQ_POLICY_TIMEOUT_SOURCE_BLOCKED;
    }
    if (config->fdcan1 >= config->tic12400_exti)
    {
        result |= APP_IRQ_POLICY_SERVICE_ORDER_INVALID;
    }
    if ((config->tic12400_exti >= config->application_timer) ||
        (config->application_timer >= config->user_button))
    {
        result |= APP_IRQ_POLICY_BACKGROUND_ORDER_INVALID;
    }

    return result;
}
