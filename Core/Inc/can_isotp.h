#ifndef CAN_ISOTP_H
#define CAN_ISOTP_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: none
 */

#include <stdint.h>

#include "can_transport.h"
#include "isotp_transport.h"

#define CAN_ISOTP_BUFFER_CAPACITY 512U
#define CAN_ISOTP_FLOW_CONTROL_TIMEOUT_US 1000000UL
#define CAN_ISOTP_CONSECUTIVE_FRAME_TIMEOUT_US 1000000UL
#define CAN_ISOTP_RX_BLOCK_SIZE 8U
#define CAN_ISOTP_RX_ST_MIN 5U
#define CAN_ISOTP_MAX_WAIT_FRAMES 1U

typedef enum
{
    CAN_ISOTP_RESULT_OK = 0,
    CAN_ISOTP_RESULT_NO_WORK,
    CAN_ISOTP_RESULT_REQUEST_READY,
    CAN_ISOTP_RESULT_BUSY,
    CAN_ISOTP_RESULT_INVALID_ARGUMENT,
    CAN_ISOTP_RESULT_PAYLOAD_TOO_LARGE,
    CAN_ISOTP_RESULT_PROTOCOL_ERROR,
    CAN_ISOTP_RESULT_RECEIVE_OVERFLOW,
    CAN_ISOTP_RESULT_TIMEOUT,
    CAN_ISOTP_RESULT_TRANSPORT_ERROR,
    CAN_ISOTP_RESULT_TRANSMIT_ABORTED
} CAN_IsoTp_Result_t;

typedef struct
{
    uint32_t rx_frames;
    uint32_t rx_messages;
    uint32_t rx_protocol_errors;
    uint32_t rx_overflows;
    uint32_t rx_busy;
    uint32_t rx_aborted;
    uint32_t sequence_errors;
    uint32_t unexpected_frames;
    uint32_t timeout_events;
    uint32_t tx_messages_started;
    uint32_t tx_messages_completed;
    uint32_t tx_busy;
    uint32_t tx_frames_enqueued;
    uint32_t tx_transport_failures;
    uint32_t tx_aborted;
    IsoTp_Result_t last_core_result;
    CAN_Transport_Result_t last_transport_result;
} CAN_IsoTp_Stats_t;

void CAN_IsoTp_Init(void);

/* The caller has already validated the CAN identifier and frame format. */
CAN_IsoTp_Result_t CAN_IsoTp_OnCanFrame(
    const uint8_t *data,
    uint8_t dlc,
    uint32_t now_us
);

/* Produces at most one consecutive frame per call. */
CAN_IsoTp_Result_t CAN_IsoTp_Process(uint32_t now_us);

CAN_IsoTp_Result_t CAN_IsoTp_StartResponse(
    const uint8_t *payload,
    uint16_t payload_length,
    uint32_t now_us
);

uint8_t CAN_IsoTp_HasRequest(void);
const uint8_t *CAN_IsoTp_GetRequestData(void);
uint16_t CAN_IsoTp_GetRequestLength(void);
void CAN_IsoTp_ReleaseRequest(void);
void CAN_IsoTp_GetStats(CAN_IsoTp_Stats_t *stats);

#endif /* CAN_ISOTP_H */
