#ifndef TIC12400_RECOVERY_H
#define TIC12400_RECOVERY_H

#include <stdint.h>

#define TIC12400_RECOVERY_FAILURE_THRESHOLD 3U
#define TIC12400_RECOVERY_INITIAL_DELAY_MS  500U
#define TIC12400_RECOVERY_MAX_DELAY_MS      8000U

typedef struct
{
    uint32_t consecutive_failures;
    uint32_t offline_events;
    uint32_t reinitialization_attempts;
    uint32_t reinitialization_successes;
    uint32_t retry_started_tick;
    uint32_t retry_delay_ms;
    uint8_t reinitialization_pending;
} TIC12400_RecoveryState_t;

void TIC12400_Recovery_Init(TIC12400_RecoveryState_t *state);

void TIC12400_Recovery_RecordInitialResult(
    TIC12400_RecoveryState_t *state,
    uint32_t now,
    uint8_t succeeded);

uint8_t TIC12400_Recovery_RecordServiceResult(
    TIC12400_RecoveryState_t *state,
    uint32_t now,
    uint8_t succeeded);

uint8_t TIC12400_Recovery_ShouldReinitialize(
    const TIC12400_RecoveryState_t *state,
    uint32_t now);

void TIC12400_Recovery_RecordReinitializationResult(
    TIC12400_RecoveryState_t *state,
    uint32_t now,
    uint8_t succeeded);

#endif /* TIC12400_RECOVERY_H */
