#ifndef CAN_COMMAND_GUARD_H
#define CAN_COMMAND_GUARD_H

#include <stdint.h>

#include "can_protocol.h"

#define CAN_COMMAND_GUARD_HISTORY_CAPACITY 16U
#define CAN_COMMAND_GUARD_SESSION_HISTORY_CAPACITY 8U

typedef enum
{
    CAN_COMMAND_GUARD_ACCEPT = 0,
    CAN_COMMAND_GUARD_DUPLICATE,
    CAN_COMMAND_GUARD_REPLAY,
    CAN_COMMAND_GUARD_SESSION_REQUIRED
} CAN_CommandGuardDecision_t;

typedef struct
{
    uint32_t session_nonce;
    uint8_t session_active;
    uint8_t session_tag;
    uint8_t last_sequence;
    uint8_t last_command;
    uint8_t last_payload[CAN_PROTOCOL_PAYLOAD_SIZE];
    uint8_t history_sequence[CAN_COMMAND_GUARD_HISTORY_CAPACITY];
    uint8_t history_payload[CAN_COMMAND_GUARD_HISTORY_CAPACITY]
                           [CAN_PROTOCOL_PAYLOAD_SIZE];
    uint8_t history_valid[CAN_COMMAND_GUARD_HISTORY_CAPACITY];
    uint8_t history_next;
    uint32_t session_nonce_history[
        CAN_COMMAND_GUARD_SESSION_HISTORY_CAPACITY];
    uint8_t session_nonce_history_valid[
        CAN_COMMAND_GUARD_SESSION_HISTORY_CAPACITY];
    uint8_t session_history_next;
} CAN_CommandGuard_t;

void CAN_CommandGuard_Init(CAN_CommandGuard_t *guard);

CAN_CommandGuardDecision_t CAN_CommandGuard_StartSession(
    CAN_CommandGuard_t *guard,
    uint32_t session_nonce,
    uint8_t session_tag,
    uint8_t sequence,
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

CAN_CommandGuardDecision_t CAN_CommandGuard_Check(
    CAN_CommandGuard_t *guard,
    uint8_t session_tag,
    uint8_t sequence,
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

CAN_CommandGuardDecision_t CAN_CommandGuard_Evaluate(
    const CAN_CommandGuard_t *guard,
    uint8_t session_tag,
    uint8_t sequence,
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

uint8_t CAN_CommandGuard_IsPrivileged(
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

#endif /* CAN_COMMAND_GUARD_H */
