#ifndef CAN_RX_HEALTH_H
#define CAN_RX_HEALTH_H

/*
 * CALL_CONTEXT_DEFAULT: INTERNAL
 * CALL_CONTEXT_ISR_SAFE: CAN_RxHealth_RecordIsr
 * CALL_CONTEXT_INTERNAL: all
 */

#include <stdint.h>

typedef enum
{
    CAN_RX_HEALTH_EVENT_NEW_MESSAGE = (1UL << 0),
    CAN_RX_HEALTH_EVENT_WATERMARK = (1UL << 1),
    CAN_RX_HEALTH_EVENT_FULL = (1UL << 2),
    CAN_RX_HEALTH_EVENT_MESSAGE_LOST = (1UL << 3)
} CAN_RxHealth_Event_t;

typedef struct
{
    uint32_t new_message_events;
    uint32_t watermark_events;
    uint32_t full_events;
    uint32_t message_lost_events;
    uint32_t max_fill_level;
} CAN_RxHealth_Stats_t;

void CAN_RxHealth_Init(void);

/* ISR-safe: fixed execution time, no HAL calls, logging or command parsing. */
void CAN_RxHealth_RecordIsr(uint32_t events, uint32_t fill_level);

void CAN_RxHealth_GetStats(CAN_RxHealth_Stats_t *stats);

#endif /* CAN_RX_HEALTH_H */
