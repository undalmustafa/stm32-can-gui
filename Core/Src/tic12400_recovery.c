#include "tic12400_recovery.h"

#include <stddef.h>

static void TIC12400_Recovery_Schedule(
    TIC12400_RecoveryState_t *state,
    uint32_t now,
    uint32_t delay_ms)
{
    state->retry_started_tick = now;
    state->retry_delay_ms = delay_ms;
    state->reinitialization_pending = 1U;
}

void TIC12400_Recovery_Init(TIC12400_RecoveryState_t *state)
{
    if (state == NULL)
    {
        return;
    }

    *state = (TIC12400_RecoveryState_t){0};
}

void TIC12400_Recovery_RecordInitialResult(
    TIC12400_RecoveryState_t *state,
    uint32_t now,
    uint8_t succeeded)
{
    if (state == NULL)
    {
        return;
    }

    state->consecutive_failures = 0U;
    if (succeeded != 0U)
    {
        state->reinitialization_pending = 0U;
        state->retry_delay_ms = 0U;
        return;
    }

    state->offline_events++;
    TIC12400_Recovery_Schedule(
        state,
        now,
        TIC12400_RECOVERY_INITIAL_DELAY_MS);
}

uint8_t TIC12400_Recovery_RecordServiceResult(
    TIC12400_RecoveryState_t *state,
    uint32_t now,
    uint8_t succeeded)
{
    if (state == NULL)
    {
        return 0U;
    }

    if (succeeded != 0U)
    {
        state->consecutive_failures = 0U;
        return 0U;
    }

    if (state->consecutive_failures < UINT32_MAX)
    {
        state->consecutive_failures++;
    }

    if ((state->reinitialization_pending != 0U) ||
        (state->consecutive_failures <
         TIC12400_RECOVERY_FAILURE_THRESHOLD))
    {
        return 0U;
    }

    state->offline_events++;
    TIC12400_Recovery_Schedule(
        state,
        now,
        TIC12400_RECOVERY_INITIAL_DELAY_MS);
    return 1U;
}

uint8_t TIC12400_Recovery_ShouldReinitialize(
    const TIC12400_RecoveryState_t *state,
    uint32_t now)
{
    if ((state == NULL) ||
        (state->reinitialization_pending == 0U))
    {
        return 0U;
    }

    return (((uint32_t)(now - state->retry_started_tick) >=
             state->retry_delay_ms) ? 1U : 0U);
}

void TIC12400_Recovery_RecordReinitializationResult(
    TIC12400_RecoveryState_t *state,
    uint32_t now,
    uint8_t succeeded)
{
    uint32_t next_delay;

    if (state == NULL)
    {
        return;
    }

    state->reinitialization_attempts++;
    if (succeeded != 0U)
    {
        state->reinitialization_successes++;
        state->consecutive_failures = 0U;
        state->reinitialization_pending = 0U;
        state->retry_delay_ms = 0U;
        return;
    }

    next_delay = state->retry_delay_ms;
    if (next_delay < TIC12400_RECOVERY_INITIAL_DELAY_MS)
    {
        next_delay = TIC12400_RECOVERY_INITIAL_DELAY_MS;
    }
    else if (next_delay < TIC12400_RECOVERY_MAX_DELAY_MS)
    {
        next_delay *= 2U;
        if (next_delay > TIC12400_RECOVERY_MAX_DELAY_MS)
        {
            next_delay = TIC12400_RECOVERY_MAX_DELAY_MS;
        }
    }

    TIC12400_Recovery_Schedule(state, now, next_delay);
}
