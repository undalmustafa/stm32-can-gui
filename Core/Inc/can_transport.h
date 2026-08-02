#ifndef CAN_TRANSPORT_H
#define CAN_TRANSPORT_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: none
 */

#include "stm32h7xx_hal.h"

#define CAN_TRANSPORT_TX_QUEUE_CAPACITY 16U
#define CAN_TRANSPORT_QUEUE_STUCK_TIMEOUT_MS 1000U

typedef enum
{
    CAN_TRANSPORT_ID_STANDARD = 0,
    CAN_TRANSPORT_ID_EXTENDED
} CAN_Transport_IdType_t;

typedef enum
{
    CAN_TRANSPORT_OK = 0,
    CAN_TRANSPORT_QUEUED,
    CAN_TRANSPORT_NOT_INITIALIZED,
    CAN_TRANSPORT_INVALID_ARGUMENT,
    CAN_TRANSPORT_INVALID_ID,
    CAN_TRANSPORT_QUEUE_FULL,
    CAN_TRANSPORT_HAL_ERROR
} CAN_Transport_Result_t;

typedef struct
{
    uint32_t sent_direct;
    uint32_t enqueued;
    uint32_t coalesced;
    uint32_t high_priority_enqueued;
    uint32_t periodic_evicted;
    uint32_t sent_from_queue;
    uint32_t queue_overflow;
    uint32_t hal_error;
    uint32_t queue_stuck_events;
    uint32_t current_pending_ms;
    uint32_t longest_pending_ms;
    uint8_t pending_count;
    uint8_t queue_high_watermark;
    uint8_t queue_stuck_active;
} CAN_Transport_Stats_t;

void CAN_Transport_Init(FDCAN_HandleTypeDef *hfdcan);
void CAN_Transport_Process(void);
void CAN_Transport_GetStats(CAN_Transport_Stats_t *stats);

CAN_Transport_Result_t CAN_Transport_SendClassic(
    uint32_t identifier,
    CAN_Transport_IdType_t id_type,
    const uint8_t data[8]
);

/*
 * Periodic/state messages should use this function. If a message with the
 * same identifier and ID type is already waiting in the software queue, its
 * payload is replaced with the newest value instead of adding a stale copy.
 */
CAN_Transport_Result_t CAN_Transport_SendClassicLatest(
    uint32_t identifier,
    CAN_Transport_IdType_t id_type,
    const uint8_t data[8]
);

/*
 * Event/response messages should use this function. High-priority messages
 * are placed ahead of periodic messages while preserving FIFO order among
 * other high-priority messages. If the queue is full, one replaceable
 * periodic message may be discarded to make room for the event.
 */
CAN_Transport_Result_t CAN_Transport_SendClassicHighPriority(
    uint32_t identifier,
    CAN_Transport_IdType_t id_type,
    const uint8_t data[8]
);

#endif /* CAN_TRANSPORT_H */
