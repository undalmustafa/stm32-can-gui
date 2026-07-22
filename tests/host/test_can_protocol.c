#include "can_protocol.h"

#include <stdio.h>

#define EXPECT(condition)                                                   \
    do                                                                      \
    {                                                                       \
        if (!(condition))                                                   \
        {                                                                   \
            printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            return 1;                                                       \
        }                                                                   \
    } while (0)

int main(void)
{
    CAN_Protocol_PwmCommand_t command = {0};
    CAN_Protocol_PwmStatus_t status = {
        .actual_frequency_hz = 1000U,
        .duty_permille = 500U,
        .enabled = 1U,
        .result = 0U,
    };
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE] = {
        CAN_PROTOCOL_CMD_PWM_CONFIG,
        CAN_PROTOCOL_PWM_FLAG_ENABLE,
        0xE8U, 0x03U, 0x00U, 0x00U,
        0xF4U, 0x01U,
    };
    uint8_t encoded[CAN_PROTOCOL_PAYLOAD_SIZE] = {0};

    EXPECT(CAN_Protocol_DecodePwmCommand(payload, &command) == 1U);
    EXPECT(command.enabled == 1U);
    EXPECT(command.frequency_hz == 1000U);
    EXPECT(command.duty_permille == 500U);

    payload[1] = 0x02U;
    EXPECT(CAN_Protocol_DecodePwmCommand(payload, &command) == 0U);
    payload[1] = CAN_PROTOCOL_PWM_FLAG_ENABLE;

    CAN_Protocol_WriteU32LE(&payload[2], 0U);
    EXPECT(CAN_Protocol_DecodePwmCommand(payload, &command) == 0U);
    CAN_Protocol_WriteU32LE(&payload[2],
                            CAN_PROTOCOL_PWM_MAX_FREQUENCY_HZ + 1U);
    EXPECT(CAN_Protocol_DecodePwmCommand(payload, &command) == 0U);
    CAN_Protocol_WriteU32LE(&payload[2], 1000U);
    CAN_Protocol_WriteU16LE(&payload[6],
                            CAN_PROTOCOL_PWM_MAX_DUTY_PERMILLE + 1U);
    EXPECT(CAN_Protocol_DecodePwmCommand(payload, &command) == 0U);

    CAN_Protocol_EncodePwmStatus(&status, encoded);
    EXPECT(encoded[0] == 1U);
    EXPECT(CAN_Protocol_ReadU32LE(&encoded[1]) == 1000U);
    EXPECT(CAN_Protocol_ReadU16LE(&encoded[5]) == 500U);
    EXPECT(encoded[7] == 0U);

    puts("PASS: CAN PWM protocol validation and encoding");
    return 0;
}
