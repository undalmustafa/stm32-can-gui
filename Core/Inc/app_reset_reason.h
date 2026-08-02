#ifndef APP_RESET_REASON_H
#define APP_RESET_REASON_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: none
 */

#include <stdint.h>

typedef enum
{
    APP_RESET_REASON_NONE = 0U,
    APP_RESET_REASON_PIN = (1UL << 0),
    APP_RESET_REASON_POWER_ON = (1UL << 1),
    APP_RESET_REASON_BROWNOUT = (1UL << 2),
    APP_RESET_REASON_SOFTWARE = (1UL << 3),
    APP_RESET_REASON_IWDG = (1UL << 4),
    APP_RESET_REASON_WWDG = (1UL << 5),
    APP_RESET_REASON_LOW_POWER = (1UL << 6)
} App_ResetReason_Flag_t;

typedef struct
{
    uint32_t decoded_flags;
    uint32_t raw_rsr;
    uint32_t capture_count;
} App_ResetReason_Snapshot_t;

/* Call once after HAL_Init(), before reset flags are cleared elsewhere. */
void App_ResetReason_Capture(void);
void App_ResetReason_GetSnapshot(App_ResetReason_Snapshot_t *snapshot);
uint8_t App_ResetReason_WasIwdgReset(void);

#endif /* APP_RESET_REASON_H */
