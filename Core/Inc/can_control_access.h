#ifndef CAN_CONTROL_ACCESS_H
#define CAN_CONTROL_ACCESS_H

/*
 * CALL_CONTEXT_DEFAULT: INTERNAL
 * CALL_CONTEXT_ISR_SAFE: CAN_ControlAccess_RequestFromIsr
 * CALL_CONTEXT_INTERNAL: all
 */

#include <stdint.h>

#define CAN_CONTROL_ACCESS_WINDOW_MS 240000UL

typedef struct
{
    volatile uint32_t request_count;
    uint32_t handled_request_count;
    uint8_t active;
    uint32_t deadline;
} CAN_ControlAccess_t;

typedef enum
{
    CAN_CONTROL_ACCESS_NO_CHANGE = 0U,
    CAN_CONTROL_ACCESS_OPENED,
    CAN_CONTROL_ACCESS_EXPIRED
} CAN_ControlAccessUpdate_t;

/* Main-loop context. */
void CAN_ControlAccess_Init(CAN_ControlAccess_t *state);

/* ISR-safe: increments one single-writer monotonic 32-bit event counter. */
void CAN_ControlAccess_RequestFromIsr(CAN_ControlAccess_t *state);

/* Main-loop context; the only function allowed to change active/deadline. */
CAN_ControlAccessUpdate_t CAN_ControlAccess_Update(
    CAN_ControlAccess_t *state,
    uint32_t now);

/* Side-effect-free queries. Call Update first in the current main-loop pass. */
uint8_t CAN_ControlAccess_IsOpen(const CAN_ControlAccess_t *state);
uint32_t CAN_ControlAccess_RemainingMs(const CAN_ControlAccess_t *state,
                                       uint32_t now);

#endif /* CAN_CONTROL_ACCESS_H */
