#ifndef CAN_COMMAND_VALIDATION_H
#define CAN_COMMAND_VALIDATION_H

/*
 * CALL_CONTEXT_DEFAULT: INTERNAL
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: all
 */

#include "can_protocol.h"

#include <stdint.h>

typedef enum
{
    CAN_COMMAND_VALIDATION_VALID = 0U,
    CAN_COMMAND_VALIDATION_UNKNOWN,
    CAN_COMMAND_VALIDATION_INVALID_PAYLOAD
} CAN_CommandValidationResult_t;

CAN_CommandValidationResult_t CAN_CommandValidation_Validate(
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

#endif /* CAN_COMMAND_VALIDATION_H */
