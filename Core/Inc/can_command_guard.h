#ifndef CAN_COMMAND_GUARD_H
#define CAN_COMMAND_GUARD_H

/*
 * CALL_CONTEXT_DEFAULT: INTERNAL
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: all
 */

#include <stdint.h>

#include "can_protocol.h"

/*
 * Return nonzero when a command changes active state and therefore requires
 * the physical B1 service-access window.
 */
uint8_t CAN_CommandGuard_IsPrivileged(
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

#endif /* CAN_COMMAND_GUARD_H */
