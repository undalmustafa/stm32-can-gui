#include "unity.h"
#include "isotp_transport.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEST_BUFFER_SIZE 256U

static IsoTp_Channel_t channel;
static IsoTp_Config_t config;
static uint8_t rx_buffer[TEST_BUFFER_SIZE];
static uint8_t tx_buffer[TEST_BUFFER_SIZE];

static IsoTp_CanFrame_t MakeFrame(uint8_t dlc)
{
    IsoTp_CanFrame_t frame = {{0U}, 0U};

    frame.dlc = dlc;
    return frame;
}

static void FillPayload(uint8_t *payload, uint16_t length)
{
    uint16_t index;

    for (index = 0U; index < length; index++)
    {
        payload[index] = (uint8_t)index;
    }
}

static IsoTp_CanFrame_t MakeFlowControl(
    uint8_t flow_status,
    uint8_t block_size,
    uint8_t st_min
)
{
    IsoTp_CanFrame_t frame = MakeFrame(8U);

    frame.data[0] = (uint8_t)(0x30U | flow_status);
    frame.data[1] = block_size;
    frame.data[2] = st_min;
    return frame;
}

static IsoTp_CanFrame_t MakeConsecutiveFrame(
    uint8_t sequence,
    const uint8_t *payload,
    uint8_t payload_length
)
{
    IsoTp_CanFrame_t frame = MakeFrame((uint8_t)(payload_length + 1U));
    uint8_t index;

    frame.data[0] = (uint8_t)(0x20U | (sequence & 0x0FU));
    for (index = 0U; index < payload_length; index++)
    {
        frame.data[index + 1U] = payload[index];
    }
    return frame;
}

static void StartMultiFrameTransmit(uint32_t now_us)
{
    uint8_t payload[8U];
    IsoTp_CanFrame_t frame;

    FillPayload(payload, sizeof(payload));
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_FRAME_READY,
        IsoTp_StartTransmit(
            &channel,
            payload,
            sizeof(payload),
            now_us,
            &frame));
}

void setUp(void)
{
    config.rx_buffer = rx_buffer;
    config.rx_buffer_capacity = sizeof(rx_buffer);
    config.tx_buffer = tx_buffer;
    config.tx_buffer_capacity = sizeof(tx_buffer);
    config.flow_control_timeout_us = 10000U;
    config.consecutive_frame_timeout_us = 5000U;
    config.rx_block_size = 2U;
    config.rx_st_min = 5U;
    config.max_wait_frames = 1U;

    TEST_ASSERT_EQUAL(ISOTP_RESULT_OK, IsoTp_Init(&channel, &config));
}

void tearDown(void)
{
}

void test_Init_rejects_invalid_timing_and_buffers(void)
{
    IsoTp_Config_t invalid = config;

    invalid.rx_buffer = NULL;
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_INVALID_ARGUMENT,
        IsoTp_Init(&channel, &invalid));

    invalid = config;
    invalid.flow_control_timeout_us = 0U;
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_INVALID_ARGUMENT,
        IsoTp_Init(&channel, &invalid));

    invalid = config;
    invalid.rx_st_min = 0x80U;
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_INVALID_ARGUMENT,
        IsoTp_Init(&channel, &invalid));
}

void test_SingleFrame_transmit_is_padded_and_complete(void)
{
    const uint8_t payload[3U] = {0x22U, 0xF1U, 0x90U};
    IsoTp_CanFrame_t frame;

    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_FRAME_READY,
        IsoTp_StartTransmit(&channel, payload, sizeof(payload), 50U, &frame));
    TEST_ASSERT_EQUAL_UINT8(8U, frame.dlc);
    TEST_ASSERT_EQUAL_UINT8(3U, frame.data[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, &frame.data[1], sizeof(payload));
    TEST_ASSERT_EACH_EQUAL_UINT8(0U, &frame.data[4], 4U);
    TEST_ASSERT_EQUAL_UINT8(1U, IsoTp_IsTransmitComplete(&channel));
}

void test_StartTransmit_rejects_zero_oversize_and_busy_requests(void)
{
    uint8_t payload[9U] = {0U};
    IsoTp_CanFrame_t frame;

    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_INVALID_ARGUMENT,
        IsoTp_StartTransmit(&channel, payload, 0U, 0U, &frame));

    config.tx_buffer_capacity = 8U;
    TEST_ASSERT_EQUAL(ISOTP_RESULT_OK, IsoTp_Init(&channel, &config));
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_PAYLOAD_TOO_LARGE,
        IsoTp_StartTransmit(&channel, payload, sizeof(payload), 0U, &frame));

    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_FRAME_READY,
        IsoTp_StartTransmit(&channel, payload, 8U, 0U, &frame));
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_BUSY,
        IsoTp_StartTransmit(&channel, payload, 8U, 0U, &frame));
}

void test_MultiFrame_transmit_obeys_block_size_and_copies_payload(void)
{
    uint8_t payload[20U];
    IsoTp_CanFrame_t frame;
    IsoTp_CanFrame_t response;

    FillPayload(payload, sizeof(payload));
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_FRAME_READY,
        IsoTp_StartTransmit(
            &channel,
            payload,
            sizeof(payload),
            100U,
            &frame));
    TEST_ASSERT_EQUAL_UINT8(0x10U, frame.data[0]);
    TEST_ASSERT_EQUAL_UINT8(20U, frame.data[1]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, &frame.data[2], 6U);

    payload[6] = 0xEEU;
    response = MakeFlowControl(0U, 1U, 0xF3U);
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_OK,
        IsoTp_OnCanFrame(&channel, &response, 200U, &frame));
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_FRAME_READY,
        IsoTp_Process(&channel, 200U, &frame));
    TEST_ASSERT_EQUAL_UINT8(0x21U, frame.data[0]);
    TEST_ASSERT_EQUAL_UINT8(6U, frame.data[1]);
    TEST_ASSERT_EQUAL(ISOTP_TX_WAIT_FLOW_CONTROL, channel.tx_state);

    response = MakeFlowControl(0U, 0U, 1U);
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_OK,
        IsoTp_OnCanFrame(&channel, &response, 300U, &frame));
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_FRAME_READY,
        IsoTp_Process(&channel, 300U, &frame));
    TEST_ASSERT_EQUAL_UINT8(0x22U, frame.data[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&tx_buffer[13], &frame.data[1], 7U);
    TEST_ASSERT_EQUAL_UINT8(1U, IsoTp_IsTransmitComplete(&channel));
}

void test_Transmit_respects_st_min_and_zero_pads_last_frame(void)
{
    uint8_t payload[15U];
    IsoTp_CanFrame_t frame;
    IsoTp_CanFrame_t flow_control;

    FillPayload(payload, sizeof(payload));
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_FRAME_READY,
        IsoTp_StartTransmit(
            &channel,
            payload,
            sizeof(payload),
            0U,
            &frame));

    flow_control = MakeFlowControl(0U, 0U, 5U);
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_OK,
        IsoTp_OnCanFrame(&channel, &flow_control, 100U, &frame));
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_FRAME_READY,
        IsoTp_Process(&channel, 100U, &frame));
    TEST_ASSERT_EQUAL(ISOTP_RESULT_WAIT, IsoTp_Process(&channel, 5099U, &frame));
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_FRAME_READY,
        IsoTp_Process(&channel, 5100U, &frame));
    TEST_ASSERT_EQUAL_UINT8(0x22U, frame.data[0]);
    TEST_ASSERT_EQUAL_UINT8(13U, frame.data[1]);
    TEST_ASSERT_EQUAL_UINT8(14U, frame.data[2]);
    TEST_ASSERT_EACH_EQUAL_UINT8(0U, &frame.data[3], 5U);
    TEST_ASSERT_EQUAL_UINT8(1U, IsoTp_IsTransmitComplete(&channel));
}

void test_Transmit_handles_wait_limit_remote_overflow_and_invalid_st_min(void)
{
    IsoTp_CanFrame_t frame;
    IsoTp_CanFrame_t flow_control;

    StartMultiFrameTransmit(0U);
    flow_control = MakeFlowControl(1U, 0U, 0U);
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_WAIT,
        IsoTp_OnCanFrame(&channel, &flow_control, 100U, &frame));
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_WAIT_LIMIT,
        IsoTp_OnCanFrame(&channel, &flow_control, 200U, &frame));

    StartMultiFrameTransmit(300U);
    flow_control = MakeFlowControl(2U, 0U, 0U);
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_REMOTE_OVERFLOW,
        IsoTp_OnCanFrame(&channel, &flow_control, 400U, &frame));

    StartMultiFrameTransmit(500U);
    flow_control = MakeFlowControl(0U, 0U, 0x80U);
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_INVALID_FRAME,
        IsoTp_OnCanFrame(&channel, &flow_control, 600U, &frame));
    TEST_ASSERT_EQUAL(ISOTP_TX_IDLE, channel.tx_state);
}

void test_Transmit_flow_control_timeout_handles_tick_wrap(void)
{
    IsoTp_CanFrame_t frame;

    StartMultiFrameTransmit(UINT32_MAX - 5000U);
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_WAIT,
        IsoTp_Process(&channel, 4998U, &frame));
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_TIMEOUT,
        IsoTp_Process(&channel, 4999U, &frame));
    TEST_ASSERT_EQUAL(ISOTP_TX_IDLE, channel.tx_state);
}

void test_SingleFrame_receive_accepts_unpadded_frame_until_released(void)
{
    IsoTp_CanFrame_t frame = MakeFrame(4U);
    IsoTp_CanFrame_t response;
    const uint8_t expected[3U] = {0x62U, 0xF1U, 0x90U};

    frame.data[0] = 3U;
    frame.data[1] = expected[0];
    frame.data[2] = expected[1];
    frame.data[3] = expected[2];
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_MESSAGE_READY,
        IsoTp_OnCanFrame(&channel, &frame, 0U, &response));
    TEST_ASSERT_EQUAL_UINT8(1U, IsoTp_HasReceivedMessage(&channel));
    TEST_ASSERT_EQUAL_UINT16(3U, IsoTp_GetReceivedLength(&channel));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        expected,
        IsoTp_GetReceivedData(&channel),
        sizeof(expected));
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_BUSY,
        IsoTp_OnCanFrame(&channel, &frame, 0U, &response));

    IsoTp_ReleaseReceivedMessage(&channel);
    TEST_ASSERT_EQUAL_UINT8(0U, IsoTp_HasReceivedMessage(&channel));
    TEST_ASSERT_NULL(IsoTp_GetReceivedData(&channel));
}

void test_SingleFrame_receive_rejects_zero_length_and_truncation(void)
{
    IsoTp_CanFrame_t frame = MakeFrame(1U);
    IsoTp_CanFrame_t response;

    frame.data[0] = 0U;
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_INVALID_FRAME,
        IsoTp_OnCanFrame(&channel, &frame, 0U, &response));

    frame.data[0] = 3U;
    frame.dlc = 3U;
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_INVALID_FRAME,
        IsoTp_OnCanFrame(&channel, &frame, 0U, &response));
}

void test_FirstFrame_receive_reports_overflow_with_flow_control(void)
{
    IsoTp_CanFrame_t frame = MakeFrame(8U);
    IsoTp_CanFrame_t response;

    config.rx_buffer_capacity = 12U;
    TEST_ASSERT_EQUAL(ISOTP_RESULT_OK, IsoTp_Init(&channel, &config));
    frame.data[0] = 0x10U;
    frame.data[1] = 13U;
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_RECEIVE_OVERFLOW,
        IsoTp_OnCanFrame(&channel, &frame, 0U, &response));
    TEST_ASSERT_EQUAL_UINT8(8U, response.dlc);
    TEST_ASSERT_EQUAL_UINT8(0x32U, response.data[0]);
    TEST_ASSERT_EACH_EQUAL_UINT8(0U, &response.data[1], 7U);
    TEST_ASSERT_EQUAL(ISOTP_RX_IDLE, channel.rx_state);
}

void test_MultiFrame_receive_reassembles_payload(void)
{
    uint8_t payload[20U];
    IsoTp_CanFrame_t frame = MakeFrame(8U);
    IsoTp_CanFrame_t response;

    FillPayload(payload, sizeof(payload));
    frame.data[0] = 0x10U;
    frame.data[1] = sizeof(payload);
    (void)memcpy(&frame.data[2], payload, 6U);
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_FRAME_READY,
        IsoTp_OnCanFrame(&channel, &frame, 100U, &response));
    TEST_ASSERT_EQUAL_UINT8(0x30U, response.data[0]);
    TEST_ASSERT_EQUAL_UINT8(2U, response.data[1]);
    TEST_ASSERT_EQUAL_UINT8(5U, response.data[2]);

    frame = MakeConsecutiveFrame(1U, &payload[6], 7U);
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_OK,
        IsoTp_OnCanFrame(&channel, &frame, 200U, &response));
    frame = MakeConsecutiveFrame(2U, &payload[13], 7U);
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_MESSAGE_READY,
        IsoTp_OnCanFrame(&channel, &frame, 300U, &response));
    TEST_ASSERT_EQUAL_UINT16(sizeof(payload), IsoTp_GetReceivedLength(&channel));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        payload,
        IsoTp_GetReceivedData(&channel),
        sizeof(payload));
}

void test_Receive_emits_flow_control_at_block_boundary(void)
{
    uint8_t payload[28U];
    IsoTp_CanFrame_t frame = MakeFrame(8U);
    IsoTp_CanFrame_t response;

    FillPayload(payload, sizeof(payload));
    frame.data[0] = 0x10U;
    frame.data[1] = sizeof(payload);
    (void)memcpy(&frame.data[2], payload, 6U);
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_FRAME_READY,
        IsoTp_OnCanFrame(&channel, &frame, 0U, &response));

    frame = MakeConsecutiveFrame(1U, &payload[6], 7U);
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_OK,
        IsoTp_OnCanFrame(&channel, &frame, 100U, &response));
    frame = MakeConsecutiveFrame(2U, &payload[13], 7U);
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_FRAME_READY,
        IsoTp_OnCanFrame(&channel, &frame, 200U, &response));
    TEST_ASSERT_EQUAL_UINT8(0x30U, response.data[0]);
    TEST_ASSERT_EQUAL_UINT8(2U, response.data[1]);
}

void test_Receive_sequence_wraps_from_fifteen_to_zero(void)
{
    uint8_t payload[112U];
    IsoTp_CanFrame_t frame = MakeFrame(8U);
    IsoTp_CanFrame_t response;
    uint8_t sequence;
    uint16_t offset;

    config.rx_block_size = 0U;
    TEST_ASSERT_EQUAL(ISOTP_RESULT_OK, IsoTp_Init(&channel, &config));
    FillPayload(payload, sizeof(payload));
    frame.data[0] = 0x10U;
    frame.data[1] = sizeof(payload);
    (void)memcpy(&frame.data[2], payload, 6U);
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_FRAME_READY,
        IsoTp_OnCanFrame(&channel, &frame, 0U, &response));

    offset = 6U;
    for (sequence = 1U; sequence <= 15U; sequence++)
    {
        frame = MakeConsecutiveFrame(sequence, &payload[offset], 7U);
        TEST_ASSERT_EQUAL(
            ISOTP_RESULT_OK,
            IsoTp_OnCanFrame(
                &channel,
                &frame,
                (uint32_t)sequence * 100U,
                &response));
        offset = (uint16_t)(offset + 7U);
    }

    frame = MakeConsecutiveFrame(0U, &payload[offset], 1U);
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_MESSAGE_READY,
        IsoTp_OnCanFrame(&channel, &frame, 1600U, &response));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        payload,
        IsoTp_GetReceivedData(&channel),
        sizeof(payload));
}

void test_Receive_sequence_error_and_timeout_abort_session(void)
{
    IsoTp_CanFrame_t frame = MakeFrame(8U);
    IsoTp_CanFrame_t response;
    uint8_t payload[7U] = {0U};

    frame.data[0] = 0x10U;
    frame.data[1] = 20U;
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_FRAME_READY,
        IsoTp_OnCanFrame(&channel, &frame, 100U, &response));
    frame = MakeConsecutiveFrame(2U, payload, sizeof(payload));
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_SEQUENCE_ERROR,
        IsoTp_OnCanFrame(&channel, &frame, 200U, &response));
    TEST_ASSERT_EQUAL(ISOTP_RX_IDLE, channel.rx_state);

    frame = MakeFrame(8U);
    frame.data[0] = 0x10U;
    frame.data[1] = 20U;
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_FRAME_READY,
        IsoTp_OnCanFrame(&channel, &frame, UINT32_MAX - 1000U, &response));
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_WAIT,
        IsoTp_Process(&channel, 3998U, &response));
    TEST_ASSERT_EQUAL(
        ISOTP_RESULT_TIMEOUT,
        IsoTp_Process(&channel, 3999U, &response));
    TEST_ASSERT_EQUAL(ISOTP_RX_IDLE, channel.rx_state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_Init_rejects_invalid_timing_and_buffers);
    RUN_TEST(test_SingleFrame_transmit_is_padded_and_complete);
    RUN_TEST(test_StartTransmit_rejects_zero_oversize_and_busy_requests);
    RUN_TEST(test_MultiFrame_transmit_obeys_block_size_and_copies_payload);
    RUN_TEST(test_Transmit_respects_st_min_and_zero_pads_last_frame);
    RUN_TEST(
        test_Transmit_handles_wait_limit_remote_overflow_and_invalid_st_min);
    RUN_TEST(test_Transmit_flow_control_timeout_handles_tick_wrap);
    RUN_TEST(
        test_SingleFrame_receive_accepts_unpadded_frame_until_released);
    RUN_TEST(test_SingleFrame_receive_rejects_zero_length_and_truncation);
    RUN_TEST(test_FirstFrame_receive_reports_overflow_with_flow_control);
    RUN_TEST(test_MultiFrame_receive_reassembles_payload);
    RUN_TEST(test_Receive_emits_flow_control_at_block_boundary);
    RUN_TEST(test_Receive_sequence_wraps_from_fifteen_to_zero);
    RUN_TEST(test_Receive_sequence_error_and_timeout_abort_session);
    return UNITY_END();
}
