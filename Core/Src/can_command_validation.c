#include "can_command_validation.h"

#include "pca2131.h"

#include <stddef.h>

CAN_CommandValidationResult_t CAN_CommandValidation_Validate(
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    uint8_t id_type;
    uint32_t can_id;
    CAN_Protocol_RtcAlarmCommand_t alarm_command;

    if (payload == NULL)
    {
        return CAN_COMMAND_VALIDATION_INVALID_PAYLOAD;
    }

    switch (payload[0])
    {
        case CAN_PROTOCOL_CMD_SET_SLOT_1:
        case CAN_PROTOCOL_CMD_SET_SLOT_2:
            if ((payload[1] & 0xFCU) != 0U)
            {
                return CAN_COMMAND_VALIDATION_INVALID_PAYLOAD;
            }

            id_type =
                ((payload[1] & CAN_PROTOCOL_SLOT_FLAG_EXTENDED_ID) != 0U)
                ? 1U
                : 0U;
            can_id = CAN_Protocol_ReadU32LE(&payload[2]);
            if ((CAN_Protocol_IsValidId(id_type, can_id) == 0U) ||
                (CAN_Protocol_ReadU16LE(&payload[6]) == 0U))
            {
                return CAN_COMMAND_VALIDATION_INVALID_PAYLOAD;
            }
            return CAN_COMMAND_VALIDATION_VALID;

        case CAN_PROTOCOL_CMD_START_SLOT_1_COUNTER:
        case CAN_PROTOCOL_CMD_START_SLOT_2_COUNTER:
            if ((payload[1] != 0U) ||
                (payload[6] != 0U) ||
                (payload[7] != 0U))
            {
                return CAN_COMMAND_VALIDATION_INVALID_PAYLOAD;
            }
            return CAN_COMMAND_VALIDATION_VALID;

        case CAN_PROTOCOL_CMD_LED_CONTROL:
            if ((payload[1] < 1U) || (payload[1] > 2U) ||
                (payload[2] > 1U) ||
                (payload[3] != 0U) ||
                (payload[4] != 0U) ||
                (payload[5] != 0U) ||
                (payload[6] != 0U) ||
                (payload[7] != 0U))
            {
                return CAN_COMMAND_VALIDATION_INVALID_PAYLOAD;
            }
            return CAN_COMMAND_VALIDATION_VALID;

        case CAN_PROTOCOL_CMD_RTC_SET_TIME:
            if ((payload[1] > 23U) ||
                (payload[2] > 59U) ||
                (payload[3] > 59U) ||
                (payload[4] != 0U) ||
                (payload[5] != 0U) ||
                (payload[6] != 0U) ||
                (payload[7] != 0U))
            {
                return CAN_COMMAND_VALIDATION_INVALID_PAYLOAD;
            }
            return CAN_COMMAND_VALIDATION_VALID;

        case CAN_PROTOCOL_CMD_RTC_SET_DATETIME:
        {
            const PCA2131_DateTime_t date_time =
            {
                .hundredth = payload[1],
                .second = payload[2],
                .minute = payload[3],
                .hour = payload[4],
                .day = payload[5],
                .weekday = (payload[6] >> 5) & 0x07U,
                .month = payload[6] & 0x1FU,
                .year = payload[7]
            };

            if (PCA2131_Driver_IsValidDateTime(&date_time) == 0U)
            {
                return CAN_COMMAND_VALIDATION_INVALID_PAYLOAD;
            }
            return CAN_COMMAND_VALIDATION_VALID;
        }

        case CAN_PROTOCOL_CMD_RTC_SET_ALARM:
            if (CAN_Protocol_DecodeRtcAlarmCommand(
                    payload, &alarm_command) == 0U)
            {
                return CAN_COMMAND_VALIDATION_INVALID_PAYLOAD;
            }
            return CAN_COMMAND_VALIDATION_VALID;

        case CAN_PROTOCOL_CMD_LOG_GET_INFO:
            if ((payload[1] != 0U) ||
                (payload[2] != 0U) ||
                (payload[3] != 0U) ||
                (payload[4] != 0U) ||
                (payload[5] != 0U) ||
                (payload[6] != 0U) ||
                (payload[7] != 0U))
            {
                return CAN_COMMAND_VALIDATION_INVALID_PAYLOAD;
            }
            return CAN_COMMAND_VALIDATION_VALID;

        case CAN_PROTOCOL_CMD_LOG_READ_SEQUENCE:
            if ((CAN_Protocol_ReadU32LE(&payload[1]) == 0U) ||
                (payload[5] != 0U) ||
                (payload[6] != 0U) ||
                (payload[7] != 0U))
            {
                return CAN_COMMAND_VALIDATION_INVALID_PAYLOAD;
            }
            return CAN_COMMAND_VALIDATION_VALID;

        case CAN_PROTOCOL_CMD_PWM_SET:
        {
            uint32_t frequency_hz =
                CAN_Protocol_ReadU32LE(&payload[1]);

            if ((frequency_hz > 1000000U) ||
                (payload[5] > 100U) ||
                (payload[6] != 0U) ||
                (payload[7] != 0U))
            {
                return CAN_COMMAND_VALIDATION_INVALID_PAYLOAD;
            }
            return CAN_COMMAND_VALIDATION_VALID;
        }

        case CAN_PROTOCOL_CMD_PWM_SELF_TEST:
            if ((payload[1] > 1U) ||
                (payload[2] != 0U) ||
                (payload[3] != 0U) ||
                (payload[4] != 0U) ||
                (payload[5] != 0U) ||
                (payload[6] != 0U) ||
                (payload[7] != 0U))
            {
                return CAN_COMMAND_VALIDATION_INVALID_PAYLOAD;
            }
            return CAN_COMMAND_VALIDATION_VALID;

        case CAN_PROTOCOL_CMD_TIC12400_SET_POLARITY:
        {
            uint32_t battery_switch_mask;

            if (CAN_Protocol_DecodeTic12400PolarityCommand(
                    payload, &battery_switch_mask) == 0U)
            {
                return CAN_COMMAND_VALIDATION_INVALID_PAYLOAD;
            }
            return CAN_COMMAND_VALIDATION_VALID;
        }

        default:
            return CAN_COMMAND_VALIDATION_UNKNOWN;
    }
}
