#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>

/**
 * @brief  Watchdog initialization result codes.
 *
 *         The IWDG uses the LSI oscillator (~32 kHz).  Once started the
 *         watchdog cannot be stopped — only a system reset will disable it.
 *         This is a safety feature, not a limitation.
 */
typedef enum
{
    WATCHDOG_OK = 0,         /**< IWDG configured and running.            */
    WATCHDOG_HAL_ERROR       /**< HAL_IWDG_Init returned an error.        */
} Watchdog_Result_t;

/**
 * @brief  Initialise and start the Independent Watchdog (IWDG).
 *
 *         Prescaler and reload values are chosen so that the watchdog
 *         timeout is approximately 1 second.  This is generous enough
 *         for the cooperative superloop (typical iteration < 1 ms) while
 *         still catching genuine hangs quickly.
 *
 *         IWDG clock source: LSI ≈ 32 kHz
 *         Prescaler: /32  → counter clock ≈ 1 kHz
 *         Reload:    999  → timeout ≈ 1000 ms
 *
 * @return WATCHDOG_OK on success, WATCHDOG_HAL_ERROR on failure.
 */
Watchdog_Result_t Watchdog_Init(void);

/**
 * @brief  Refresh (kick) the watchdog counter.
 *
 *         Must be called from the superloop at least once per timeout
 *         period.  If the superloop stalls, the IWDG will reset the MCU.
 */
void Watchdog_Refresh(void);

/**
 * @brief  Check whether the last reset was caused by the IWDG.
 *
 *         Reads and then clears the RCC reset flags.  Call once during
 *         early initialisation if you want to log or report watchdog
 *         resets.
 *
 * @return 1 if the previous reset was a watchdog reset, 0 otherwise.
 */
uint8_t Watchdog_Was_Reset_By_Watchdog(void);

#endif /* WATCHDOG_H */
