#include "can_isotp.h"

#include "can_protocol_generated.h"

#include <stddef.h>

static IsoTp_Channel_t isotp_channel;
static uint8_t isotp_rx_buffer[CAN_ISOTP_BUFFER_CAPACITY];
static uint8_t isotp_tx_buffer[CAN_ISOTP_BUFFER_CAPACITY];
static CAN_IsoTp_Stats_t isotp_stats;

static CAN_IsoTp_Result_t CAN_IsoTp_SendFrame(
    const IsoTp_CanFrame_t *frame,
    uint8_t abort_receive_on_failure
)
{
    CAN_Transport_Result_t transport_result;

    transport_result = CAN_Transport_SendClassicHighPriority(
        CAN_PROTOCOL_DIAGNOSTIC_RESPONSE_TX_ID,
        CAN_TRANSPORT_ID_STANDARD,
        frame->data);
    isotp_stats.last_transport_result = transport_result;

    if ((transport_result == CAN_TRANSPORT_OK) ||
        (transport_result == CAN_TRANSPORT_QUEUED))
    {
        isotp_stats.tx_frames_enqueued++;
        return CAN_ISOTP_RESULT_OK;
    }

    isotp_stats.tx_transport_failures++;
    if (abort_receive_on_failure != 0U)
    {
        isotp_stats.rx_aborted++;
        IsoTp_CancelReceive(&isotp_channel);
    }
    else
    {
        isotp_stats.tx_aborted++;
        IsoTp_CancelTransmit(&isotp_channel);
    }
    return CAN_ISOTP_RESULT_TRANSPORT_ERROR;
}

static CAN_IsoTp_Result_t CAN_IsoTp_RecordCoreError(
    IsoTp_Result_t result
)
{
    switch (result)
    {
        case ISOTP_RESULT_BUSY:
            isotp_stats.rx_busy++;
            return CAN_ISOTP_RESULT_BUSY;

        case ISOTP_RESULT_SEQUENCE_ERROR:
            isotp_stats.sequence_errors++;
            isotp_stats.rx_protocol_errors++;
            return CAN_ISOTP_RESULT_PROTOCOL_ERROR;

        case ISOTP_RESULT_UNEXPECTED_FRAME:
            isotp_stats.unexpected_frames++;
            isotp_stats.rx_protocol_errors++;
            return CAN_ISOTP_RESULT_PROTOCOL_ERROR;

        case ISOTP_RESULT_TIMEOUT:
            isotp_stats.timeout_events++;
            return CAN_ISOTP_RESULT_TIMEOUT;

        case ISOTP_RESULT_PAYLOAD_TOO_LARGE:
            return CAN_ISOTP_RESULT_PAYLOAD_TOO_LARGE;

        case ISOTP_RESULT_REMOTE_OVERFLOW:
        case ISOTP_RESULT_WAIT_LIMIT:
            return CAN_ISOTP_RESULT_TRANSMIT_ABORTED;

        case ISOTP_RESULT_INVALID_ARGUMENT:
            return CAN_ISOTP_RESULT_INVALID_ARGUMENT;

        case ISOTP_RESULT_INVALID_FRAME:
        default:
            isotp_stats.rx_protocol_errors++;
            return CAN_ISOTP_RESULT_PROTOCOL_ERROR;
    }
}

void CAN_IsoTp_Init(void)
{
    IsoTp_Config_t config;

    isotp_stats = (CAN_IsoTp_Stats_t){0};
    isotp_stats.last_transport_result = CAN_TRANSPORT_NOT_INITIALIZED;

    config.rx_buffer = isotp_rx_buffer;
    config.rx_buffer_capacity = sizeof(isotp_rx_buffer);
    config.tx_buffer = isotp_tx_buffer;
    config.tx_buffer_capacity = sizeof(isotp_tx_buffer);
    config.flow_control_timeout_us = CAN_ISOTP_FLOW_CONTROL_TIMEOUT_US;
    config.consecutive_frame_timeout_us =
        CAN_ISOTP_CONSECUTIVE_FRAME_TIMEOUT_US;
    config.rx_block_size = CAN_ISOTP_RX_BLOCK_SIZE;
    config.rx_st_min = CAN_ISOTP_RX_ST_MIN;
    config.max_wait_frames = CAN_ISOTP_MAX_WAIT_FRAMES;

    isotp_stats.last_core_result = IsoTp_Init(&isotp_channel, &config);
}

CAN_IsoTp_Result_t CAN_IsoTp_OnCanFrame(
    const uint8_t *data,
    uint8_t dlc,
    uint32_t now_us
)
{
    IsoTp_CanFrame_t input_frame = {{0U}, 0U};
    IsoTp_CanFrame_t response_frame;
    IsoTp_Result_t core_result;
    CAN_IsoTp_Result_t send_result;
    uint8_t index;
    uint8_t receive_was_active;
    uint8_t transmit_was_active;

    if ((data == NULL) || (dlc == 0U) ||
        (dlc > ISOTP_CLASSIC_CAN_FRAME_SIZE))
    {
        return CAN_ISOTP_RESULT_INVALID_ARGUMENT;
    }

    input_frame.dlc = dlc;
    for (index = 0U; index < dlc; index++)
    {
        input_frame.data[index] = data[index];
    }

    receive_was_active = (uint8_t)(
        isotp_channel.rx_state == ISOTP_RX_RECEIVING);
    transmit_was_active = (uint8_t)(
        (isotp_channel.tx_state == ISOTP_TX_WAIT_FLOW_CONTROL) ||
        (isotp_channel.tx_state == ISOTP_TX_SEND_CONSECUTIVE));
    isotp_stats.rx_frames++;
    core_result = IsoTp_OnCanFrame(
        &isotp_channel,
        &input_frame,
        now_us,
        &response_frame);
    isotp_stats.last_core_result = core_result;
    if ((receive_was_active != 0U) &&
        (isotp_channel.rx_state == ISOTP_RX_IDLE))
    {
        isotp_stats.rx_aborted++;
    }
    if ((transmit_was_active != 0U) &&
        (isotp_channel.tx_state == ISOTP_TX_IDLE))
    {
        isotp_stats.tx_aborted++;
    }

    if (core_result == ISOTP_RESULT_MESSAGE_READY)
    {
        isotp_stats.rx_messages++;
        return CAN_ISOTP_RESULT_REQUEST_READY;
    }

    if ((core_result == ISOTP_RESULT_FRAME_READY) ||
        (core_result == ISOTP_RESULT_RECEIVE_OVERFLOW))
    {
        if (core_result == ISOTP_RESULT_RECEIVE_OVERFLOW)
        {
            isotp_stats.rx_overflows++;
        }

        send_result = CAN_IsoTp_SendFrame(&response_frame, 1U);
        if (send_result != CAN_ISOTP_RESULT_OK)
        {
            return send_result;
        }

        return (core_result == ISOTP_RESULT_RECEIVE_OVERFLOW)
                   ? CAN_ISOTP_RESULT_RECEIVE_OVERFLOW
                   : CAN_ISOTP_RESULT_OK;
    }

    if ((core_result == ISOTP_RESULT_OK) ||
        (core_result == ISOTP_RESULT_WAIT))
    {
        return CAN_ISOTP_RESULT_OK;
    }

    return CAN_IsoTp_RecordCoreError(core_result);
}

CAN_IsoTp_Result_t CAN_IsoTp_Process(uint32_t now_us)
{
    IsoTp_CanFrame_t frame;
    IsoTp_Result_t core_result;
    CAN_IsoTp_Result_t send_result;
    uint8_t receive_was_active;
    uint8_t transmit_was_active;

    receive_was_active = (uint8_t)(
        isotp_channel.rx_state == ISOTP_RX_RECEIVING);
    transmit_was_active = (uint8_t)(
        (isotp_channel.tx_state == ISOTP_TX_WAIT_FLOW_CONTROL) ||
        (isotp_channel.tx_state == ISOTP_TX_SEND_CONSECUTIVE));
    core_result = IsoTp_Process(&isotp_channel, now_us, &frame);
    isotp_stats.last_core_result = core_result;
    if ((receive_was_active != 0U) &&
        (isotp_channel.rx_state == ISOTP_RX_IDLE))
    {
        isotp_stats.rx_aborted++;
    }
    if ((transmit_was_active != 0U) &&
        (isotp_channel.tx_state == ISOTP_TX_IDLE))
    {
        isotp_stats.tx_aborted++;
    }

    if (core_result == ISOTP_RESULT_WAIT)
    {
        return CAN_ISOTP_RESULT_NO_WORK;
    }

    if (core_result == ISOTP_RESULT_FRAME_READY)
    {
        send_result = CAN_IsoTp_SendFrame(&frame, 0U);
        if (send_result != CAN_ISOTP_RESULT_OK)
        {
            return send_result;
        }

        if (IsoTp_IsTransmitComplete(&isotp_channel) != 0U)
        {
            isotp_stats.tx_messages_completed++;
        }
        return CAN_ISOTP_RESULT_OK;
    }

    return CAN_IsoTp_RecordCoreError(core_result);
}

CAN_IsoTp_Result_t CAN_IsoTp_StartResponse(
    const uint8_t *payload,
    uint16_t payload_length,
    uint32_t now_us
)
{
    IsoTp_CanFrame_t frame;
    IsoTp_Result_t core_result;
    CAN_IsoTp_Result_t send_result;

    core_result = IsoTp_StartTransmit(
        &isotp_channel,
        payload,
        payload_length,
        now_us,
        &frame);
    isotp_stats.last_core_result = core_result;
    if (core_result == ISOTP_RESULT_BUSY)
    {
        isotp_stats.tx_busy++;
        return CAN_ISOTP_RESULT_BUSY;
    }
    if (core_result != ISOTP_RESULT_FRAME_READY)
    {
        return CAN_IsoTp_RecordCoreError(core_result);
    }

    isotp_stats.tx_messages_started++;
    send_result = CAN_IsoTp_SendFrame(&frame, 0U);
    if (send_result != CAN_ISOTP_RESULT_OK)
    {
        return send_result;
    }

    if (IsoTp_IsTransmitComplete(&isotp_channel) != 0U)
    {
        isotp_stats.tx_messages_completed++;
    }
    return CAN_ISOTP_RESULT_OK;
}

uint8_t CAN_IsoTp_HasRequest(void)
{
    return IsoTp_HasReceivedMessage(&isotp_channel);
}

const uint8_t *CAN_IsoTp_GetRequestData(void)
{
    return IsoTp_GetReceivedData(&isotp_channel);
}

uint16_t CAN_IsoTp_GetRequestLength(void)
{
    return IsoTp_GetReceivedLength(&isotp_channel);
}

void CAN_IsoTp_ReleaseRequest(void)
{
    IsoTp_ReleaseReceivedMessage(&isotp_channel);
}

void CAN_IsoTp_GetStats(CAN_IsoTp_Stats_t *stats)
{
    if (stats != NULL)
    {
        *stats = isotp_stats;
    }
}
