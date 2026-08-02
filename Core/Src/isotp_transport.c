#include "isotp_transport.h"

#include <limits.h>
#include <stddef.h>

#define ISOTP_PCI_TYPE_MASK 0xF0U
#define ISOTP_PCI_SINGLE_FRAME 0x00U
#define ISOTP_PCI_FIRST_FRAME 0x10U
#define ISOTP_PCI_CONSECUTIVE_FRAME 0x20U
#define ISOTP_PCI_FLOW_CONTROL 0x30U

#define ISOTP_FLOW_STATUS_CONTINUE 0x00U
#define ISOTP_FLOW_STATUS_WAIT 0x01U
#define ISOTP_FLOW_STATUS_OVERFLOW 0x02U

#define ISOTP_SINGLE_FRAME_PAYLOAD_SIZE 7U
#define ISOTP_FIRST_FRAME_PAYLOAD_SIZE 6U
#define ISOTP_CONSECUTIVE_FRAME_PAYLOAD_SIZE 7U
#define ISOTP_SEQUENCE_MASK 0x0FU

static void IsoTp_ClearFrame(IsoTp_CanFrame_t *frame)
{
    uint8_t index;

    for (index = 0U; index < ISOTP_CLASSIC_CAN_FRAME_SIZE; index++)
    {
        frame->data[index] = 0U;
    }
    frame->dlc = ISOTP_CLASSIC_CAN_FRAME_SIZE;
}

static uint8_t IsoTp_DeadlineReached(uint32_t now_us, uint32_t deadline_us)
{
    return (uint8_t)(((int32_t)(now_us - deadline_us)) >= 0);
}

static uint8_t IsoTp_DecodeStMin(uint8_t encoded, uint32_t *st_min_us)
{
    if (encoded <= 0x7FU)
    {
        *st_min_us = (uint32_t)encoded * 1000U;
        return 1U;
    }

    if ((encoded >= 0xF1U) && (encoded <= 0xF9U))
    {
        *st_min_us = (uint32_t)(encoded - 0xF0U) * 100U;
        return 1U;
    }

    return 0U;
}

static void IsoTp_ResetReceiveState(IsoTp_Channel_t *channel)
{
    channel->rx_state = ISOTP_RX_IDLE;
    channel->rx_length = 0U;
    channel->rx_offset = 0U;
    channel->rx_deadline_us = 0U;
    channel->rx_next_sequence = 0U;
    channel->rx_block_count = 0U;
}

static void IsoTp_ResetTransmitState(IsoTp_Channel_t *channel)
{
    channel->tx_state = ISOTP_TX_IDLE;
    channel->tx_length = 0U;
    channel->tx_offset = 0U;
    channel->tx_deadline_us = 0U;
    channel->tx_st_min_us = 0U;
    channel->tx_next_sequence = 0U;
    channel->tx_block_remaining = 0U;
    channel->tx_wait_count = 0U;
}

static void IsoTp_BuildFlowControl(
    const IsoTp_Channel_t *channel,
    uint8_t flow_status,
    IsoTp_CanFrame_t *frame
)
{
    IsoTp_ClearFrame(frame);
    frame->data[0] = (uint8_t)(ISOTP_PCI_FLOW_CONTROL | flow_status);

    if (flow_status == ISOTP_FLOW_STATUS_CONTINUE)
    {
        frame->data[1] = channel->config.rx_block_size;
        frame->data[2] = channel->config.rx_st_min;
    }
}

static IsoTp_Result_t IsoTp_ReceiveSingleFrame(
    IsoTp_Channel_t *channel,
    const IsoTp_CanFrame_t *frame
)
{
    uint8_t payload_length;
    uint8_t index;

    if (channel->rx_state != ISOTP_RX_IDLE)
    {
        return ISOTP_RESULT_BUSY;
    }

    payload_length = (uint8_t)(frame->data[0] & 0x0FU);
    if ((payload_length == 0U) ||
        (payload_length > ISOTP_SINGLE_FRAME_PAYLOAD_SIZE) ||
        ((uint16_t)payload_length > channel->config.rx_buffer_capacity) ||
        ((uint8_t)(payload_length + 1U) > frame->dlc))
    {
        return ISOTP_RESULT_INVALID_FRAME;
    }

    for (index = 0U; index < payload_length; index++)
    {
        channel->config.rx_buffer[index] = frame->data[index + 1U];
    }

    channel->rx_length = payload_length;
    channel->rx_offset = payload_length;
    channel->rx_state = ISOTP_RX_COMPLETE;
    return ISOTP_RESULT_MESSAGE_READY;
}

static IsoTp_Result_t IsoTp_ReceiveFirstFrame(
    IsoTp_Channel_t *channel,
    const IsoTp_CanFrame_t *frame,
    uint32_t now_us,
    IsoTp_CanFrame_t *response_frame
)
{
    uint16_t payload_length;
    uint8_t index;

    if (channel->rx_state != ISOTP_RX_IDLE)
    {
        return ISOTP_RESULT_BUSY;
    }

    if (frame->dlc != ISOTP_CLASSIC_CAN_FRAME_SIZE)
    {
        return ISOTP_RESULT_INVALID_FRAME;
    }

    payload_length = (uint16_t)(
        ((uint16_t)(frame->data[0] & 0x0FU) << 8U) |
        (uint16_t)frame->data[1]);
    if (payload_length <= ISOTP_SINGLE_FRAME_PAYLOAD_SIZE)
    {
        return ISOTP_RESULT_INVALID_FRAME;
    }

    if (payload_length > channel->config.rx_buffer_capacity)
    {
        IsoTp_BuildFlowControl(
            channel,
            ISOTP_FLOW_STATUS_OVERFLOW,
            response_frame);
        return ISOTP_RESULT_RECEIVE_OVERFLOW;
    }

    for (index = 0U; index < ISOTP_FIRST_FRAME_PAYLOAD_SIZE; index++)
    {
        channel->config.rx_buffer[index] = frame->data[index + 2U];
    }

    channel->rx_length = payload_length;
    channel->rx_offset = ISOTP_FIRST_FRAME_PAYLOAD_SIZE;
    channel->rx_next_sequence = 1U;
    channel->rx_block_count = 0U;
    channel->rx_deadline_us =
        now_us + channel->config.consecutive_frame_timeout_us;
    channel->rx_state = ISOTP_RX_RECEIVING;
    IsoTp_BuildFlowControl(
        channel,
        ISOTP_FLOW_STATUS_CONTINUE,
        response_frame);
    return ISOTP_RESULT_FRAME_READY;
}

static IsoTp_Result_t IsoTp_ReceiveConsecutiveFrame(
    IsoTp_Channel_t *channel,
    const IsoTp_CanFrame_t *frame,
    uint32_t now_us,
    IsoTp_CanFrame_t *response_frame
)
{
    uint16_t remaining;
    uint8_t copy_length;
    uint8_t sequence;
    uint8_t index;

    if (channel->rx_state != ISOTP_RX_RECEIVING)
    {
        return ISOTP_RESULT_UNEXPECTED_FRAME;
    }

    if (IsoTp_DeadlineReached(now_us, channel->rx_deadline_us) != 0U)
    {
        IsoTp_ResetReceiveState(channel);
        return ISOTP_RESULT_TIMEOUT;
    }

    sequence = (uint8_t)(frame->data[0] & ISOTP_SEQUENCE_MASK);
    if (sequence != channel->rx_next_sequence)
    {
        IsoTp_ResetReceiveState(channel);
        return ISOTP_RESULT_SEQUENCE_ERROR;
    }

    remaining = (uint16_t)(channel->rx_length - channel->rx_offset);
    copy_length = (remaining > ISOTP_CONSECUTIVE_FRAME_PAYLOAD_SIZE)
                      ? ISOTP_CONSECUTIVE_FRAME_PAYLOAD_SIZE
                      : (uint8_t)remaining;
    if ((frame->dlc < 2U) || ((uint8_t)(copy_length + 1U) > frame->dlc))
    {
        IsoTp_ResetReceiveState(channel);
        return ISOTP_RESULT_INVALID_FRAME;
    }

    for (index = 0U; index < copy_length; index++)
    {
        channel->config.rx_buffer[channel->rx_offset + index] =
            frame->data[index + 1U];
    }

    channel->rx_offset = (uint16_t)(channel->rx_offset + copy_length);
    channel->rx_next_sequence =
        (uint8_t)((channel->rx_next_sequence + 1U) & ISOTP_SEQUENCE_MASK);

    if (channel->rx_offset == channel->rx_length)
    {
        channel->rx_state = ISOTP_RX_COMPLETE;
        channel->rx_deadline_us = 0U;
        return ISOTP_RESULT_MESSAGE_READY;
    }

    channel->rx_deadline_us =
        now_us + channel->config.consecutive_frame_timeout_us;
    if (channel->config.rx_block_size != 0U)
    {
        channel->rx_block_count++;
        if (channel->rx_block_count >= channel->config.rx_block_size)
        {
            channel->rx_block_count = 0U;
            IsoTp_BuildFlowControl(
                channel,
                ISOTP_FLOW_STATUS_CONTINUE,
                response_frame);
            return ISOTP_RESULT_FRAME_READY;
        }
    }

    return ISOTP_RESULT_OK;
}

static IsoTp_Result_t IsoTp_ReceiveFlowControl(
    IsoTp_Channel_t *channel,
    const IsoTp_CanFrame_t *frame,
    uint32_t now_us
)
{
    uint8_t flow_status;
    uint32_t st_min_us;

    if (channel->tx_state != ISOTP_TX_WAIT_FLOW_CONTROL)
    {
        return ISOTP_RESULT_UNEXPECTED_FRAME;
    }

    if (IsoTp_DeadlineReached(now_us, channel->tx_deadline_us) != 0U)
    {
        IsoTp_ResetTransmitState(channel);
        return ISOTP_RESULT_TIMEOUT;
    }

    if (frame->dlc < 3U)
    {
        IsoTp_ResetTransmitState(channel);
        return ISOTP_RESULT_INVALID_FRAME;
    }

    flow_status = (uint8_t)(frame->data[0] & 0x0FU);
    if (flow_status == ISOTP_FLOW_STATUS_CONTINUE)
    {
        if (IsoTp_DecodeStMin(frame->data[2], &st_min_us) == 0U)
        {
            IsoTp_ResetTransmitState(channel);
            return ISOTP_RESULT_INVALID_FRAME;
        }

        channel->tx_block_remaining = frame->data[1];
        channel->tx_st_min_us = st_min_us;
        channel->tx_deadline_us = now_us;
        channel->tx_wait_count = 0U;
        channel->tx_state = ISOTP_TX_SEND_CONSECUTIVE;
        return ISOTP_RESULT_OK;
    }

    if (flow_status == ISOTP_FLOW_STATUS_WAIT)
    {
        if (channel->tx_wait_count >= channel->config.max_wait_frames)
        {
            IsoTp_ResetTransmitState(channel);
            return ISOTP_RESULT_WAIT_LIMIT;
        }

        channel->tx_wait_count++;
        channel->tx_deadline_us =
            now_us + channel->config.flow_control_timeout_us;
        return ISOTP_RESULT_WAIT;
    }

    IsoTp_ResetTransmitState(channel);
    if (flow_status == ISOTP_FLOW_STATUS_OVERFLOW)
    {
        return ISOTP_RESULT_REMOTE_OVERFLOW;
    }

    return ISOTP_RESULT_INVALID_FRAME;
}

IsoTp_Result_t IsoTp_Init(
    IsoTp_Channel_t *channel,
    const IsoTp_Config_t *config
)
{
    uint32_t ignored_st_min_us;

    if ((channel == NULL) || (config == NULL) ||
        (config->rx_buffer == NULL) || (config->tx_buffer == NULL) ||
        (config->rx_buffer_capacity == 0U) ||
        (config->tx_buffer_capacity == 0U) ||
        (config->flow_control_timeout_us == 0U) ||
        (config->flow_control_timeout_us > (uint32_t)INT32_MAX) ||
        (config->consecutive_frame_timeout_us == 0U) ||
        (config->consecutive_frame_timeout_us > (uint32_t)INT32_MAX) ||
        (IsoTp_DecodeStMin(config->rx_st_min, &ignored_st_min_us) == 0U))
    {
        return ISOTP_RESULT_INVALID_ARGUMENT;
    }

    *channel = (IsoTp_Channel_t){0};
    channel->config = *config;
    channel->rx_state = ISOTP_RX_IDLE;
    channel->tx_state = ISOTP_TX_IDLE;
    channel->initialized = 1U;
    return ISOTP_RESULT_OK;
}

IsoTp_Result_t IsoTp_StartTransmit(
    IsoTp_Channel_t *channel,
    const uint8_t *payload,
    uint16_t payload_length,
    uint32_t now_us,
    IsoTp_CanFrame_t *frame
)
{
    uint16_t index;

    if ((channel == NULL) || (payload == NULL) || (frame == NULL) ||
        (channel->initialized == 0U) || (payload_length == 0U))
    {
        return ISOTP_RESULT_INVALID_ARGUMENT;
    }

    if ((payload_length > ISOTP_CLASSIC_MAX_PAYLOAD_LENGTH) ||
        (payload_length > channel->config.tx_buffer_capacity))
    {
        return ISOTP_RESULT_PAYLOAD_TOO_LARGE;
    }

    if ((channel->tx_state == ISOTP_TX_WAIT_FLOW_CONTROL) ||
        (channel->tx_state == ISOTP_TX_SEND_CONSECUTIVE))
    {
        return ISOTP_RESULT_BUSY;
    }

    for (index = 0U; index < payload_length; index++)
    {
        channel->config.tx_buffer[index] = payload[index];
    }

    IsoTp_ClearFrame(frame);
    channel->tx_length = payload_length;
    channel->tx_wait_count = 0U;

    if (payload_length <= ISOTP_SINGLE_FRAME_PAYLOAD_SIZE)
    {
        frame->data[0] = (uint8_t)payload_length;
        for (index = 0U; index < payload_length; index++)
        {
            frame->data[index + 1U] = channel->config.tx_buffer[index];
        }
        channel->tx_offset = payload_length;
        channel->tx_state = ISOTP_TX_COMPLETE;
        return ISOTP_RESULT_FRAME_READY;
    }

    frame->data[0] = (uint8_t)(
        ISOTP_PCI_FIRST_FRAME | ((payload_length >> 8U) & 0x0FU));
    frame->data[1] = (uint8_t)payload_length;
    for (index = 0U; index < ISOTP_FIRST_FRAME_PAYLOAD_SIZE; index++)
    {
        frame->data[index + 2U] = channel->config.tx_buffer[index];
    }

    channel->tx_offset = ISOTP_FIRST_FRAME_PAYLOAD_SIZE;
    channel->tx_next_sequence = 1U;
    channel->tx_block_remaining = 0U;
    channel->tx_st_min_us = 0U;
    channel->tx_deadline_us =
        now_us + channel->config.flow_control_timeout_us;
    channel->tx_state = ISOTP_TX_WAIT_FLOW_CONTROL;
    return ISOTP_RESULT_FRAME_READY;
}

IsoTp_Result_t IsoTp_OnCanFrame(
    IsoTp_Channel_t *channel,
    const IsoTp_CanFrame_t *frame,
    uint32_t now_us,
    IsoTp_CanFrame_t *response_frame
)
{
    uint8_t pci_type;

    if ((channel == NULL) || (frame == NULL) || (response_frame == NULL) ||
        (channel->initialized == 0U) || (frame->dlc == 0U) ||
        (frame->dlc > ISOTP_CLASSIC_CAN_FRAME_SIZE))
    {
        return ISOTP_RESULT_INVALID_ARGUMENT;
    }

    pci_type = (uint8_t)(frame->data[0] & ISOTP_PCI_TYPE_MASK);
    switch (pci_type)
    {
        case ISOTP_PCI_SINGLE_FRAME:
            return IsoTp_ReceiveSingleFrame(channel, frame);

        case ISOTP_PCI_FIRST_FRAME:
            return IsoTp_ReceiveFirstFrame(
                channel,
                frame,
                now_us,
                response_frame);

        case ISOTP_PCI_CONSECUTIVE_FRAME:
            return IsoTp_ReceiveConsecutiveFrame(
                channel,
                frame,
                now_us,
                response_frame);

        case ISOTP_PCI_FLOW_CONTROL:
            return IsoTp_ReceiveFlowControl(channel, frame, now_us);

        default:
            return ISOTP_RESULT_INVALID_FRAME;
    }
}

IsoTp_Result_t IsoTp_Process(
    IsoTp_Channel_t *channel,
    uint32_t now_us,
    IsoTp_CanFrame_t *frame
)
{
    uint16_t remaining;
    uint8_t copy_length;
    uint8_t index;

    if ((channel == NULL) || (frame == NULL) ||
        (channel->initialized == 0U))
    {
        return ISOTP_RESULT_INVALID_ARGUMENT;
    }

    if ((channel->rx_state == ISOTP_RX_RECEIVING) &&
        (IsoTp_DeadlineReached(now_us, channel->rx_deadline_us) != 0U))
    {
        IsoTp_ResetReceiveState(channel);
        return ISOTP_RESULT_TIMEOUT;
    }

    if ((channel->tx_state == ISOTP_TX_WAIT_FLOW_CONTROL) &&
        (IsoTp_DeadlineReached(now_us, channel->tx_deadline_us) != 0U))
    {
        IsoTp_ResetTransmitState(channel);
        return ISOTP_RESULT_TIMEOUT;
    }

    if (channel->tx_state != ISOTP_TX_SEND_CONSECUTIVE)
    {
        return ISOTP_RESULT_WAIT;
    }

    if (IsoTp_DeadlineReached(now_us, channel->tx_deadline_us) == 0U)
    {
        return ISOTP_RESULT_WAIT;
    }

    remaining = (uint16_t)(channel->tx_length - channel->tx_offset);
    copy_length = (remaining > ISOTP_CONSECUTIVE_FRAME_PAYLOAD_SIZE)
                      ? ISOTP_CONSECUTIVE_FRAME_PAYLOAD_SIZE
                      : (uint8_t)remaining;

    IsoTp_ClearFrame(frame);
    frame->data[0] = (uint8_t)(
        ISOTP_PCI_CONSECUTIVE_FRAME | channel->tx_next_sequence);
    for (index = 0U; index < copy_length; index++)
    {
        frame->data[index + 1U] =
            channel->config.tx_buffer[channel->tx_offset + index];
    }

    channel->tx_offset = (uint16_t)(channel->tx_offset + copy_length);
    channel->tx_next_sequence =
        (uint8_t)((channel->tx_next_sequence + 1U) & ISOTP_SEQUENCE_MASK);

    if (channel->tx_offset == channel->tx_length)
    {
        channel->tx_state = ISOTP_TX_COMPLETE;
        channel->tx_deadline_us = 0U;
        return ISOTP_RESULT_FRAME_READY;
    }

    if (channel->tx_block_remaining != 0U)
    {
        channel->tx_block_remaining--;
        if (channel->tx_block_remaining == 0U)
        {
            channel->tx_state = ISOTP_TX_WAIT_FLOW_CONTROL;
            channel->tx_deadline_us =
                now_us + channel->config.flow_control_timeout_us;
            return ISOTP_RESULT_FRAME_READY;
        }
    }

    channel->tx_deadline_us = now_us + channel->tx_st_min_us;
    return ISOTP_RESULT_FRAME_READY;
}

uint8_t IsoTp_HasReceivedMessage(const IsoTp_Channel_t *channel)
{
    return (uint8_t)((channel != NULL) &&
                     (channel->initialized != 0U) &&
                     (channel->rx_state == ISOTP_RX_COMPLETE));
}

const uint8_t *IsoTp_GetReceivedData(const IsoTp_Channel_t *channel)
{
    if (IsoTp_HasReceivedMessage(channel) == 0U)
    {
        return NULL;
    }

    return channel->config.rx_buffer;
}

uint16_t IsoTp_GetReceivedLength(const IsoTp_Channel_t *channel)
{
    if (IsoTp_HasReceivedMessage(channel) == 0U)
    {
        return 0U;
    }

    return channel->rx_length;
}

uint8_t IsoTp_IsTransmitComplete(const IsoTp_Channel_t *channel)
{
    return (uint8_t)((channel != NULL) &&
                     (channel->initialized != 0U) &&
                     (channel->tx_state == ISOTP_TX_COMPLETE));
}

void IsoTp_ReleaseReceivedMessage(IsoTp_Channel_t *channel)
{
    if ((channel != NULL) && (channel->initialized != 0U) &&
        (channel->rx_state == ISOTP_RX_COMPLETE))
    {
        IsoTp_ResetReceiveState(channel);
    }
}

void IsoTp_CancelReceive(IsoTp_Channel_t *channel)
{
    if ((channel != NULL) && (channel->initialized != 0U))
    {
        IsoTp_ResetReceiveState(channel);
    }
}

void IsoTp_CancelTransmit(IsoTp_Channel_t *channel)
{
    if ((channel != NULL) && (channel->initialized != 0U))
    {
        IsoTp_ResetTransmitState(channel);
    }
}
