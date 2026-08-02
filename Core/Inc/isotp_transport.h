#ifndef ISOTP_TRANSPORT_H
#define ISOTP_TRANSPORT_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: none
 */

#include <stdint.h>

#define ISOTP_CLASSIC_CAN_FRAME_SIZE 8U
#define ISOTP_CLASSIC_MAX_PAYLOAD_LENGTH 4095U

typedef enum
{
    ISOTP_RESULT_OK = 0,
    ISOTP_RESULT_FRAME_READY,
    ISOTP_RESULT_MESSAGE_READY,
    ISOTP_RESULT_WAIT,
    ISOTP_RESULT_INVALID_ARGUMENT,
    ISOTP_RESULT_INVALID_FRAME,
    ISOTP_RESULT_PAYLOAD_TOO_LARGE,
    ISOTP_RESULT_RECEIVE_OVERFLOW,
    ISOTP_RESULT_REMOTE_OVERFLOW,
    ISOTP_RESULT_BUSY,
    ISOTP_RESULT_UNEXPECTED_FRAME,
    ISOTP_RESULT_SEQUENCE_ERROR,
    ISOTP_RESULT_TIMEOUT,
    ISOTP_RESULT_WAIT_LIMIT
} IsoTp_Result_t;

typedef enum
{
    ISOTP_RX_IDLE = 0,
    ISOTP_RX_RECEIVING,
    ISOTP_RX_COMPLETE
} IsoTp_RxState_t;

typedef enum
{
    ISOTP_TX_IDLE = 0,
    ISOTP_TX_WAIT_FLOW_CONTROL,
    ISOTP_TX_SEND_CONSECUTIVE,
    ISOTP_TX_COMPLETE
} IsoTp_TxState_t;

typedef struct
{
    uint8_t data[ISOTP_CLASSIC_CAN_FRAME_SIZE];
    uint8_t dlc;
} IsoTp_CanFrame_t;

typedef struct
{
    uint8_t *rx_buffer;
    uint16_t rx_buffer_capacity;
    uint8_t *tx_buffer;
    uint16_t tx_buffer_capacity;
    uint32_t flow_control_timeout_us;
    uint32_t consecutive_frame_timeout_us;
    uint8_t rx_block_size;
    uint8_t rx_st_min;
    uint8_t max_wait_frames;
} IsoTp_Config_t;

typedef struct
{
    IsoTp_Config_t config;
    IsoTp_RxState_t rx_state;
    IsoTp_TxState_t tx_state;
    uint16_t rx_length;
    uint16_t rx_offset;
    uint16_t tx_length;
    uint16_t tx_offset;
    uint32_t rx_deadline_us;
    uint32_t tx_deadline_us;
    uint32_t tx_st_min_us;
    uint8_t rx_next_sequence;
    uint8_t rx_block_count;
    uint8_t tx_next_sequence;
    uint8_t tx_block_remaining;
    uint8_t tx_wait_count;
    uint8_t initialized;
} IsoTp_Channel_t;

/*
 * Buffers are caller-owned and must remain valid for the channel lifetime.
 * Timeout values must be non-zero and no greater than INT32_MAX microseconds.
 */
IsoTp_Result_t IsoTp_Init(
    IsoTp_Channel_t *channel,
    const IsoTp_Config_t *config
);

/*
 * Copies the complete payload into the configured TX buffer. The returned
 * frame is always an eight-byte, zero-padded Classic CAN data frame.
 */
IsoTp_Result_t IsoTp_StartTransmit(
    IsoTp_Channel_t *channel,
    const uint8_t *payload,
    uint16_t payload_length,
    uint32_t now_us,
    IsoTp_CanFrame_t *frame
);

/*
 * Consumes one ISO-TP CAN frame. FRAME_READY and RECEIVE_OVERFLOW both mean
 * that response_frame contains a flow-control frame that must be transmitted.
 */
IsoTp_Result_t IsoTp_OnCanFrame(
    IsoTp_Channel_t *channel,
    const IsoTp_CanFrame_t *frame,
    uint32_t now_us,
    IsoTp_CanFrame_t *response_frame
);

/*
 * Services N_Bs/N_Cr timeouts and produces paced consecutive TX frames.
 */
IsoTp_Result_t IsoTp_Process(
    IsoTp_Channel_t *channel,
    uint32_t now_us,
    IsoTp_CanFrame_t *frame
);

uint8_t IsoTp_HasReceivedMessage(const IsoTp_Channel_t *channel);
const uint8_t *IsoTp_GetReceivedData(const IsoTp_Channel_t *channel);
uint16_t IsoTp_GetReceivedLength(const IsoTp_Channel_t *channel);
uint8_t IsoTp_IsTransmitComplete(const IsoTp_Channel_t *channel);

void IsoTp_ReleaseReceivedMessage(IsoTp_Channel_t *channel);
void IsoTp_CancelReceive(IsoTp_Channel_t *channel);
void IsoTp_CancelTransmit(IsoTp_Channel_t *channel);

#endif /* ISOTP_TRANSPORT_H */
