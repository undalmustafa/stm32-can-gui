#include "main.h"
#include "watchdog.h"

/**
 * @file watchdog.c
 * @brief Independent Watchdog (IWDG) driver for the STM32H7A3.
 *
 * Design notes
 * ────────────
 * The IWDG runs on the LSI oscillator (≈ 32 kHz, uncalibrated).
 * We choose prescaler /32 and reload 999 to obtain a timeout of
 * approximately 1 second:
 *
 *     timeout = (reload + 1) × prescaler / f_LSI
 *             = 1000 × 32 / 32000
 *             = 1.0 s
 *
 * The cooperative superloop completes in well under 1 ms, so a 1 s
 * timeout gives three orders of magnitude of margin while still
 * catching real stalls quickly.
 *
 * Once started the IWDG cannot be stopped — this is an intentional
 * hardware safety property.  If `Error_Handler()` disables interrupts
 * and spins, the IWDG will reset the MCU within ~1 s, which is the
 * desired recovery behaviour.
 *
 * The module deliberately does NOT blink an LED or send a CAN frame
 * on watchdog reset.  CAN bus state is unknown at that point, and LED
 * blinking belongs to the application layer.  The caller (main.c) can
 * query `Watchdog_Was_Reset_By_Watchdog()` and react as appropriate.
 */

static IWDG_HandleTypeDef hiwdg;

Watchdog_Result_t Watchdog_Init(void)
{
    /*
     * IWDG clock source: LSI ≈ 32 kHz (hardware-fixed, no mux)
     *
     * Prescaler  : IWDG_PRESCALER_32  → counter clock ≈ 1 kHz
     * Reload     : 999                → timeout ≈ 1000 ms
     * Window     : IWDG_WINDOW_DISABLE (0x0FFF) — no window restriction
     *
     * Window mode is disabled because the superloop has no deterministic
     * lower bound on cycle time.  We only need an upper-bound guard.
     */
    hiwdg.Instance       = IWDG1;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload    = 999U;
    hiwdg.Init.Window    = IWDG_WINDOW_DISABLE;

    if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
    {
        return WATCHDOG_HAL_ERROR;
    }

    return WATCHDOG_OK;
}

void Watchdog_Refresh(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}

uint8_t Watchdog_Was_Reset_By_Watchdog(void)
{
    uint8_t was_iwdg = 0U;

    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDG1RST) != 0U)
    {
        was_iwdg = 1U;
    }

    /* Clear all reset flags so the next reset source is unambiguous. */
    __HAL_RCC_CLEAR_RESET_FLAGS();

    return was_iwdg;
}
