#include "can_command_guard.h"

#include <stddef.h>

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
            return 0U;

        case CAN_PROTOCOL_CMD_SET_SLOT_1:
        case CAN_PROTOCOL_CMD_SET_SLOT_2:
            return (uint8_t)(
                ((payload[1] & CAN_PROTOCOL_SLOT_FLAG_ENABLE) == 0U)
                ? 0U
                : 1U);

        case CAN_PROTOCOL_CMD_START_SLOT_1_COUNTER:
        case CAN_PROTOCOL_CMD_START_SLOT_2_COUNTER:
            return (uint8_t)(
                (CAN_Protocol_ReadU32LE(&payload[2]) == 0U) ? 0U : 1U);

        case CAN_PROTOCOL_CMD_LED_CONTROL:
            return (uint8_t)((payload[2] == 0U) ? 0U : 1U);

        case CAN_PROTOCOL_CMD_PWM_SET:
            return (uint8_t)(
                (CAN_Protocol_ReadU32LE(&payload[1]) == 0U) ? 0U : 1U);

        case CAN_PROTOCOL_CMD_PWM_SELF_TEST:
            return (uint8_t)((payload[1] == 0U) ? 0U : 1U);

        case CAN_PROTOCOL_CMD_TIC12400_SET_POLARITY:
            return 1U;

        default:
            return 1U;
    }
}
