#ifndef APP_WATCHDOG_EVIDENCE_H
#define APP_WATCHDOG_EVIDENCE_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: none
 */

#include "app_watchdog.h"

#include <stdint.h>

#define APP_WATCHDOG_EVIDENCE_MAGIC          0x57444752UL
#define APP_WATCHDOG_EVIDENCE_VERSION        1U
#define APP_WATCHDOG_EVIDENCE_COMMIT_MARKER  0xC35AA53CUL

typedef enum
{
    APP_WATCHDOG_EVIDENCE_NONE = 0U,
    APP_WATCHDOG_EVIDENCE_HARD_STALL = 1U,
    APP_WATCHDOG_EVIDENCE_HEALTH_GATE_REJECTED = 2U,
    APP_WATCHDOG_EVIDENCE_REFRESH_ERROR = 3U
} App_Watchdog_EvidenceCause_t;

typedef struct
{
    uint32_t sequence;
    uint32_t cause;
    uint32_t required_heartbeat_mask;
    uint32_t observed_heartbeat_mask;
    uint32_t missing_heartbeat_mask;
    uint32_t health_check_tick;
    uint32_t refresh_tick;
    uint32_t capture_count;
    uint32_t validation_error_count;
    uint32_t stale_record_count;
    uint8_t valid;
    uint8_t storage_ready;
} App_Watchdog_ResetEvidence_t;

/* Exposed for debugger inspection. Application code must use the API. */
extern volatile App_Watchdog_ResetEvidence_t g_appWatchdogResetEvidence;

void App_Watchdog_Evidence_CaptureBoot(uint8_t was_iwdg_reset);
void App_Watchdog_Evidence_Record(
    App_Watchdog_EvidenceCause_t cause,
    uint32_t required_heartbeat_mask,
    uint32_t observed_heartbeat_mask,
    uint32_t missing_heartbeat_mask,
    uint32_t health_check_tick,
    uint32_t refresh_tick);
void App_Watchdog_Evidence_Get(App_Watchdog_ResetEvidence_t *evidence);

#if APP_WATCHDOG_TEST_HOOKS_ENABLED
void App_Watchdog_Evidence_TestResetStorage(void);
void App_Watchdog_Evidence_TestCorruptChecksum(void);
#endif

#endif /* APP_WATCHDOG_EVIDENCE_H */
