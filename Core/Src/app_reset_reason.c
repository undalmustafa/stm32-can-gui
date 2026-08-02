#include "main.h"
#include "app_reset_reason.h"

#include <stddef.h>

static volatile App_ResetReason_Snapshot_t g_appResetReasonSnapshot;

static uint32_t App_ResetReason_Decode(uint32_t raw_rsr)
{
    uint32_t decoded = APP_RESET_REASON_NONE;

#if defined(RCC_RSR_PINRSTF)
    if ((raw_rsr & RCC_RSR_PINRSTF) != 0U)
    {
        decoded |= APP_RESET_REASON_PIN;
    }
#endif

#if defined(RCC_RSR_PORRSTF)
    if ((raw_rsr & RCC_RSR_PORRSTF) != 0U)
    {
        decoded |= APP_RESET_REASON_POWER_ON;
    }
#endif

#if defined(RCC_RSR_BORRSTF)
    if ((raw_rsr & RCC_RSR_BORRSTF) != 0U)
    {
        decoded |= APP_RESET_REASON_BROWNOUT;
    }
#endif

#if defined(RCC_RSR_SFTRSTF)
    if ((raw_rsr & RCC_RSR_SFTRSTF) != 0U)
    {
        decoded |= APP_RESET_REASON_SOFTWARE;
    }
#endif

#if defined(RCC_RSR_IWDG1RSTF)
    if ((raw_rsr & RCC_RSR_IWDG1RSTF) != 0U)
    {
        decoded |= APP_RESET_REASON_IWDG;
    }
#endif

#if defined(RCC_RSR_WWDG1RSTF)
    if ((raw_rsr & RCC_RSR_WWDG1RSTF) != 0U)
    {
        decoded |= APP_RESET_REASON_WWDG;
    }
#endif

#if defined(RCC_RSR_LPWRRSTF)
    if ((raw_rsr & RCC_RSR_LPWRRSTF) != 0U)
    {
        decoded |= APP_RESET_REASON_LOW_POWER;
    }
#endif

    return decoded;
}

void App_ResetReason_Capture(void)
{
    uint32_t raw_rsr = RCC->RSR;

    g_appResetReasonSnapshot.raw_rsr = raw_rsr;
    g_appResetReasonSnapshot.decoded_flags =
        App_ResetReason_Decode(raw_rsr);
    g_appResetReasonSnapshot.capture_count++;

    /* Reset causes are sticky. Clear them only after the snapshot is stored. */
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

void App_ResetReason_GetSnapshot(App_ResetReason_Snapshot_t *snapshot)
{
    if (snapshot != NULL)
    {
        snapshot->decoded_flags =
            g_appResetReasonSnapshot.decoded_flags;
        snapshot->raw_rsr = g_appResetReasonSnapshot.raw_rsr;
        snapshot->capture_count =
            g_appResetReasonSnapshot.capture_count;
    }
}

uint8_t App_ResetReason_WasIwdgReset(void)
{
    return ((g_appResetReasonSnapshot.decoded_flags &
             APP_RESET_REASON_IWDG) != 0U) ? 1U : 0U;
}
