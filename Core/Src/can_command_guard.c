#include "can_command_guard.h"

#include <stddef.h>
#include <string.h>

static void CAN_CommandGuard_Record(
    CAN_CommandGuard_t *guard,
    uint8_t sequence,
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    uint8_t index = guard->history_next;

    guard->last_sequence = sequence;
    guard->last_command = payload[0];
    memcpy(guard->last_payload, payload, CAN_PROTOCOL_PAYLOAD_SIZE);

    guard->history_sequence[index] = sequence;
    memcpy(guard->history_payload[index],
           payload,
           CAN_PROTOCOL_PAYLOAD_SIZE);
    guard->history_valid[index] = 1U;
    guard->history_next = (uint8_t)(
        (index + 1U) % CAN_COMMAND_GUARD_HISTORY_CAPACITY);
}

static CAN_CommandGuardDecision_t CAN_CommandGuard_EvaluateSequence(
    const CAN_CommandGuard_t *guard,
    uint8_t sequence,
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    uint8_t distance;
    uint8_t index;

    for (index = 0U;
         index < CAN_COMMAND_GUARD_HISTORY_CAPACITY;
         index++)
    {
        if ((guard->history_valid[index] != 0U) &&
            (guard->history_sequence[index] == sequence))
        {
            return (memcmp(payload,
                           guard->history_payload[index],
                           CAN_PROTOCOL_PAYLOAD_SIZE) == 0)
                 ? CAN_COMMAND_GUARD_DUPLICATE
                 : CAN_COMMAND_GUARD_REPLAY;
        }
    }

    distance = (uint8_t)(sequence - guard->last_sequence);

    /*
     * Modulo-256 values one to 127 positions ahead are new. Values in the
     * opposite half of the ring are stale or out-of-order replays.
     */
    if (distance >= 128U)
    {
        return CAN_COMMAND_GUARD_REPLAY;
    }

    return CAN_COMMAND_GUARD_ACCEPT;
}

void CAN_CommandGuard_Init(CAN_CommandGuard_t *guard)
{
    if (guard != NULL)
    {
        *guard = (CAN_CommandGuard_t){0};
    }
}

CAN_CommandGuardDecision_t CAN_CommandGuard_StartSession(
    CAN_CommandGuard_t *guard,
    uint32_t session_nonce,
    uint8_t session_tag,
    uint8_t sequence,
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    uint8_t index;

    if ((guard == NULL) || (payload == NULL) || (session_nonce == 0U))
    {
        return CAN_COMMAND_GUARD_REPLAY;
    }

    if ((guard->session_active != 0U) &&
        (guard->session_nonce == session_nonce) &&
        (guard->session_tag == session_tag))
    {
        CAN_CommandGuardDecision_t decision =
            CAN_CommandGuard_EvaluateSequence(guard, sequence, payload);

        if (decision == CAN_COMMAND_GUARD_ACCEPT)
        {
            CAN_CommandGuard_Record(guard, sequence, payload);
        }
        return decision;
    }

    for (index = 0U;
         index < CAN_COMMAND_GUARD_SESSION_HISTORY_CAPACITY;
         index++)
    {
        if ((guard->session_nonce_history_valid[index] != 0U) &&
            (guard->session_nonce_history[index] == session_nonce))
        {
            return CAN_COMMAND_GUARD_REPLAY;
        }
    }

    index = guard->session_history_next;
    guard->session_nonce_history[index] = session_nonce;
    guard->session_nonce_history_valid[index] = 1U;
    guard->session_history_next = (uint8_t)(
        (index + 1U) % CAN_COMMAND_GUARD_SESSION_HISTORY_CAPACITY);

    guard->session_nonce = session_nonce;
    guard->session_active = 1U;
    guard->session_tag = session_tag;
    memset(guard->history_valid, 0, sizeof(guard->history_valid));
    guard->history_next = 0U;
    CAN_CommandGuard_Record(guard, sequence, payload);
    return CAN_COMMAND_GUARD_ACCEPT;
}

CAN_CommandGuardDecision_t CAN_CommandGuard_Check(
    CAN_CommandGuard_t *guard,
    uint8_t session_tag,
    uint8_t sequence,
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    CAN_CommandGuardDecision_t decision;

    if ((guard == NULL) || (payload == NULL))
    {
        return CAN_COMMAND_GUARD_REPLAY;
    }

    if (guard->session_active == 0U)
    {
        return CAN_COMMAND_GUARD_SESSION_REQUIRED;
    }

    if (guard->session_tag != session_tag)
    {
        return CAN_COMMAND_GUARD_REPLAY;
    }

    decision = CAN_CommandGuard_EvaluateSequence(guard, sequence, payload);
    if (decision == CAN_COMMAND_GUARD_ACCEPT)
    {
        CAN_CommandGuard_Record(guard, sequence, payload);
    }
    return decision;
}

CAN_CommandGuardDecision_t CAN_CommandGuard_Evaluate(
    const CAN_CommandGuard_t *guard,
    uint8_t session_tag,
    uint8_t sequence,
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    if ((guard == NULL) || (payload == NULL))
    {
        return CAN_COMMAND_GUARD_REPLAY;
    }

    if (guard->session_active == 0U)
    {
        return CAN_COMMAND_GUARD_SESSION_REQUIRED;
    }

    if (guard->session_tag != session_tag)
    {
        return CAN_COMMAND_GUARD_REPLAY;
    }

    return CAN_CommandGuard_EvaluateSequence(guard, sequence, payload);
}

uint8_t CAN_CommandGuard_IsPrivileged(
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    if (payload == NULL)
    {
        return 0U;
    }

    switch (payload[0])
    {
        case CAN_PROTOCOL_CMD_LOG_GET_INFO:
        case CAN_PROTOCOL_CMD_LOG_READ_SEQUENCE:
        case CAN_PROTOCOL_CMD_SESSION_START:
            return 0U;

        case CAN_PROTOCOL_CMD_SET_SLOT_1:
        case CAN_PROTOCOL_CMD_SET_SLOT_2:
            return ((payload[1] & CAN_PROTOCOL_SLOT_FLAG_ENABLE) == 0U)
                 ? 0U
                 : 1U;

        case CAN_PROTOCOL_CMD_START_SLOT_1_COUNTER:
        case CAN_PROTOCOL_CMD_START_SLOT_2_COUNTER:
            return (CAN_Protocol_ReadU32LE(&payload[2]) == 0U) ? 0U : 1U;

        case CAN_PROTOCOL_CMD_LED_CONTROL:
            return (payload[2] == 0U) ? 0U : 1U;

        case CAN_PROTOCOL_CMD_PWM_SET:
            return (CAN_Protocol_ReadU32LE(&payload[1]) == 0U) ? 0U : 1U;

        case CAN_PROTOCOL_CMD_PWM_SELF_TEST:
            return (payload[1] == 0U) ? 0U : 1U;

        default:
            return 1U;
    }
}
