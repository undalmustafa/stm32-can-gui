#ifndef CAN_COMMAND_GUARD_H
#define CAN_COMMAND_GUARD_H

#include <stdint.h>

#include "can_protocol.h"

/*
 * Return nonzero when a command changes active state and therefore requires
 * the physical B1 service-access window.
 */
uint8_t CAN_CommandGuard_IsPrivileged(
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

#endif /* CAN_COMMAND_GUARD_H */
