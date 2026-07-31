#include "can_control_access.h"

#include <stddef.h>

void CAN_ControlAccess_Init(CAN_ControlAccess_t *state)
{
    if (state != NULL)
    {
        state->request_count = 0U;
        state->handled_request_count = 0U;
        state->active = 0U;
        state->deadline = 0U;
    }
}

void CAN_ControlAccess_RequestFromIsr(CAN_ControlAccess_t *state)
{
    if (state != NULL)
    {
        state->request_count++;
    }
}

CAN_ControlAccessUpdate_t CAN_ControlAccess_Update(
    CAN_ControlAccess_t *state,
    uint32_t now)
{
    if (state == NULL)
    {
        return CAN_CONTROL_ACCESS_NO_CHANGE;
    }

    if (state->request_count != state->handled_request_count)
    {
        state->handled_request_count = state->request_count;
        state->active = 1U;
        state->deadline = now + CAN_CONTROL_ACCESS_WINDOW_MS;
        return CAN_CONTROL_ACCESS_OPENED;
    }

    if ((state->active != 0U) &&
        ((int32_t)(state->deadline - now) <= 0))
    {
        state->active = 0U;
        return CAN_CONTROL_ACCESS_EXPIRED;
    }

    return CAN_CONTROL_ACCESS_NO_CHANGE;
}

uint8_t CAN_ControlAccess_IsOpen(const CAN_ControlAccess_t *state)
{
    return ((state != NULL) && (state->active != 0U)) ? 1U : 0U;
}

uint32_t CAN_ControlAccess_RemainingMs(const CAN_ControlAccess_t *state,
                                       uint32_t now)
{
    if ((state == NULL) ||
        (state->active == 0U) ||
        ((int32_t)(state->deadline - now) <= 0))
    {
        return 0U;
    }

    return state->deadline - now;
}
