#include "unity.h"

#include "can_isotp.h"
#include "can_protocol_generated.h"

#include <string.h>

#define TEST_TX_CAPACITY 16U

typedef struct
{
    uint32_t identifier;
    CAN_Transport_IdType_t id_type;
    uint8_t data[8U];
} Test_TxFrame_t;

static Test_TxFrame_t tx_frames[TEST_TX_CAPACITY];
static uint32_t tx_count;
static CAN_Transport_Result_t transport_result;

CAN_Transport_Result_t CAN_Transport_SendClassicHighPriority(
    uint32_t identifier,
    CAN_Transport_IdType_t id_type,
    const uint8_t data[8U]
)
{
    if (tx_count < TEST_TX_CAPACITY)
    {
        tx_frames[tx_count].identifier = identifier;
        tx_frames[tx_count].id_type = id_type;
        (void)memcpy(tx_frames[tx_count].data, data, 8U);
        tx_count++;
    }
    return transport_result;
}

static void FillPayload(uint8_t *payload, uint16_t length)
{
    uint16_t index;

    for (index = 0U; index < length; index++)
    {
        payload[index] = (uint8_t)index;
    }
}

static void StartReceive(uint16_t payload_length, uint32_t now_us)
{
    uint8_t frame[8U] = {0U};
    uint8_t index;

    frame[0] = (uint8_t)(0x10U | ((payload_length >> 8U) & 0x0FU));
    frame[1] = (uint8_t)payload_length;
    for (index = 0U; index < 6U; index++)
    {
        frame[index + 2U] = index;
    }

    TEST_ASSERT_EQUAL(
        CAN_ISOTP_RESULT_OK,
        CAN_IsoTp_OnCanFrame(frame, sizeof(frame), now_us));
}

void setUp(void)
{
    tx_count = 0U;
    transport_result = CAN_TRANSPORT_OK;
    (void)memset(tx_frames, 0, sizeof(tx_frames));
    CAN_IsoTp_Init();
}

void tearDown(void)
{
}

void test_SingleFrame_request_is_exposed_until_released(void)
{
    const uint8_t frame[4U] = {3U, 0x22U, 0xF1U, 0x90U};
    const uint8_t expected[3U] = {0x22U, 0xF1U, 0x90U};
    CAN_IsoTp_Stats_t stats;

    TEST_ASSERT_EQUAL(
        CAN_ISOTP_RESULT_REQUEST_READY,
        CAN_IsoTp_OnCanFrame(frame, sizeof(frame), 100U));
    TEST_ASSERT_EQUAL_UINT8(1U, CAN_IsoTp_HasRequest());
    TEST_ASSERT_EQUAL_UINT16(3U, CAN_IsoTp_GetRequestLength());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        expected,
        CAN_IsoTp_GetRequestData(),
        sizeof(expected));
    TEST_ASSERT_EQUAL_UINT32(0U, tx_count);

    CAN_IsoTp_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.rx_frames);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.rx_messages);

    CAN_IsoTp_ReleaseRequest();
    TEST_ASSERT_EQUAL_UINT8(0U, CAN_IsoTp_HasRequest());
}

void test_FirstFrame_enqueues_configured_flow_control(void)
{
    CAN_IsoTp_Stats_t stats;

    StartReceive(20U, 100U);
    TEST_ASSERT_EQUAL_UINT32(1U, tx_count);
    TEST_ASSERT_EQUAL_HEX32(
        CAN_PROTOCOL_DIAGNOSTIC_RESPONSE_TX_ID,
        tx_frames[0].identifier);
    TEST_ASSERT_EQUAL(CAN_TRANSPORT_ID_STANDARD, tx_frames[0].id_type);
    TEST_ASSERT_EQUAL_HEX8(0x30U, tx_frames[0].data[0]);
    TEST_ASSERT_EQUAL_UINT8(CAN_ISOTP_RX_BLOCK_SIZE, tx_frames[0].data[1]);
    TEST_ASSERT_EQUAL_UINT8(CAN_ISOTP_RX_ST_MIN, tx_frames[0].data[2]);

    CAN_IsoTp_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.tx_frames_enqueued);
    TEST_ASSERT_EQUAL(CAN_TRANSPORT_OK, stats.last_transport_result);
}

void test_MultiFrame_request_is_reassembled(void)
{
    uint8_t payload[20U];
    uint8_t frame[8U];

    FillPayload(payload, sizeof(payload));
    StartReceive(sizeof(payload), 0U);

    frame[0] = 0x21U;
    (void)memcpy(&frame[1], &payload[6], 7U);
    TEST_ASSERT_EQUAL(
        CAN_ISOTP_RESULT_OK,
        CAN_IsoTp_OnCanFrame(frame, sizeof(frame), 100U));

    frame[0] = 0x22U;
    (void)memcpy(&frame[1], &payload[13], 7U);
    TEST_ASSERT_EQUAL(
        CAN_ISOTP_RESULT_REQUEST_READY,
        CAN_IsoTp_OnCanFrame(frame, sizeof(frame), 200U));
    TEST_ASSERT_EQUAL_UINT16(sizeof(payload), CAN_IsoTp_GetRequestLength());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        payload,
        CAN_IsoTp_GetRequestData(),
        sizeof(payload));
}

void test_Receive_overflow_enqueues_overflow_flow_control(void)
{
    uint8_t frame[8U] = {0x12U, 0x01U, 0U, 0U, 0U, 0U, 0U, 0U};
    CAN_IsoTp_Stats_t stats;

    TEST_ASSERT_EQUAL(
        CAN_ISOTP_RESULT_RECEIVE_OVERFLOW,
        CAN_IsoTp_OnCanFrame(frame, sizeof(frame), 0U));
    TEST_ASSERT_EQUAL_UINT32(1U, tx_count);
    TEST_ASSERT_EQUAL_HEX8(0x32U, tx_frames[0].data[0]);

    CAN_IsoTp_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.rx_overflows);
}

void test_SingleFrame_response_uses_high_priority_transport(void)
{
    const uint8_t response[3U] = {0x7FU, 0x22U, 0x31U};
    CAN_IsoTp_Stats_t stats;

    TEST_ASSERT_EQUAL(
        CAN_ISOTP_RESULT_OK,
        CAN_IsoTp_StartResponse(response, sizeof(response), 100U));
    TEST_ASSERT_EQUAL_UINT32(1U, tx_count);
    TEST_ASSERT_EQUAL_HEX8(3U, tx_frames[0].data[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(response, &tx_frames[0].data[1], 3U);
    TEST_ASSERT_EACH_EQUAL_UINT8(0U, &tx_frames[0].data[4], 4U);

    CAN_IsoTp_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.tx_messages_started);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.tx_messages_completed);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.tx_frames_enqueued);
}

void test_MultiFrame_response_obeys_flow_control_and_pacing(void)
{
    uint8_t response[15U];
    const uint8_t flow_control[8U] =
        {0x30U, 0U, 5U, 0U, 0U, 0U, 0U, 0U};
    CAN_IsoTp_Stats_t stats;

    FillPayload(response, sizeof(response));
    TEST_ASSERT_EQUAL(
        CAN_ISOTP_RESULT_OK,
        CAN_IsoTp_StartResponse(response, sizeof(response), 0U));
    TEST_ASSERT_EQUAL_HEX8(0x10U, tx_frames[0].data[0]);
    TEST_ASSERT_EQUAL_UINT8(15U, tx_frames[0].data[1]);

    TEST_ASSERT_EQUAL(
        CAN_ISOTP_RESULT_OK,
        CAN_IsoTp_OnCanFrame(flow_control, sizeof(flow_control), 100U));
    TEST_ASSERT_EQUAL(CAN_ISOTP_RESULT_OK, CAN_IsoTp_Process(100U));
    TEST_ASSERT_EQUAL_HEX8(0x21U, tx_frames[1].data[0]);
    TEST_ASSERT_EQUAL(
        CAN_ISOTP_RESULT_NO_WORK,
        CAN_IsoTp_Process(5099U));
    TEST_ASSERT_EQUAL(CAN_ISOTP_RESULT_OK, CAN_IsoTp_Process(5100U));
    TEST_ASSERT_EQUAL_HEX8(0x22U, tx_frames[2].data[0]);

    CAN_IsoTp_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.tx_messages_started);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.tx_messages_completed);
    TEST_ASSERT_EQUAL_UINT32(3U, stats.tx_frames_enqueued);
}

void test_Transport_backpressure_aborts_session_and_is_visible(void)
{
    uint8_t response[8U] = {0U};
    CAN_IsoTp_Stats_t stats;

    transport_result = CAN_TRANSPORT_QUEUE_FULL;
    TEST_ASSERT_EQUAL(
        CAN_ISOTP_RESULT_TRANSPORT_ERROR,
        CAN_IsoTp_StartResponse(response, sizeof(response), 0U));

    CAN_IsoTp_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.tx_transport_failures);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.tx_aborted);
    TEST_ASSERT_EQUAL(CAN_TRANSPORT_QUEUE_FULL, stats.last_transport_result);

    TEST_ASSERT_EQUAL(
        CAN_ISOTP_RESULT_TRANSPORT_ERROR,
        CAN_IsoTp_OnCanFrame(
            (const uint8_t[8U]){0x10U, 20U, 0U, 0U, 0U, 0U, 0U, 0U},
            8U,
            100U));
    CAN_IsoTp_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(2U, stats.tx_transport_failures);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.tx_aborted);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.rx_aborted);
}

void test_Sequence_error_and_timeout_are_counted(void)
{
    uint8_t invalid_sequence[8U] =
        {0x22U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    CAN_IsoTp_Stats_t stats;

    StartReceive(20U, 0U);
    TEST_ASSERT_EQUAL(
        CAN_ISOTP_RESULT_PROTOCOL_ERROR,
        CAN_IsoTp_OnCanFrame(invalid_sequence, sizeof(invalid_sequence), 100U));

    StartReceive(20U, 200U);
    TEST_ASSERT_EQUAL(
        CAN_ISOTP_RESULT_TIMEOUT,
        CAN_IsoTp_Process(1000200U));

    CAN_IsoTp_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.sequence_errors);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.timeout_events);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.rx_protocol_errors);
    TEST_ASSERT_EQUAL_UINT32(2U, stats.rx_aborted);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_SingleFrame_request_is_exposed_until_released);
    RUN_TEST(test_FirstFrame_enqueues_configured_flow_control);
    RUN_TEST(test_MultiFrame_request_is_reassembled);
    RUN_TEST(test_Receive_overflow_enqueues_overflow_flow_control);
    RUN_TEST(test_SingleFrame_response_uses_high_priority_transport);
    RUN_TEST(test_MultiFrame_response_obeys_flow_control_and_pacing);
    RUN_TEST(test_Transport_backpressure_aborts_session_and_is_visible);
    RUN_TEST(test_Sequence_error_and_timeout_are_counted);
    return UNITY_END();
}
