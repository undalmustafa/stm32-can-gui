#include "unity.h"

#include "app_diagnostics.h"
#include "app_reset_reason.h"
#include "app_startup.h"
#include "can_uds.h"

#include <string.h>

static uint8_t request_buffer[64U];
static uint16_t request_length;
static uint8_t request_ready;
static uint32_t release_calls;
static uint8_t response_buffer[CAN_ISOTP_BUFFER_CAPACITY];
static uint16_t response_length;
static uint32_t response_now_us;
static uint32_t response_calls;
static CAN_IsoTp_Result_t response_result;
static App_Startup_Snapshot_t startup_snapshot;
static App_Diagnostics_Snapshot_t diagnostics_snapshot;
static App_ResetReason_Snapshot_t reset_snapshot;
static CAN_IsoTp_Stats_t isotp_stats;

uint8_t CAN_IsoTp_HasRequest(void)
{
    return request_ready;
}

const uint8_t *CAN_IsoTp_GetRequestData(void)
{
    return (request_ready != 0U) ? request_buffer : NULL;
}

uint16_t CAN_IsoTp_GetRequestLength(void)
{
    return (request_ready != 0U) ? request_length : 0U;
}

void CAN_IsoTp_ReleaseRequest(void)
{
    request_ready = 0U;
    release_calls++;
}

CAN_IsoTp_Result_t CAN_IsoTp_StartResponse(
    const uint8_t *payload,
    uint16_t payload_length,
    uint32_t now_us)
{
    response_calls++;
    response_length = payload_length;
    response_now_us = now_us;
    (void)memcpy(response_buffer, payload, payload_length);
    return response_result;
}

void CAN_IsoTp_GetStats(CAN_IsoTp_Stats_t *stats)
{
    *stats = isotp_stats;
}

void App_Startup_GetSnapshot(App_Startup_Snapshot_t *snapshot)
{
    *snapshot = startup_snapshot;
}

void App_Diagnostics_GetSnapshot(App_Diagnostics_Snapshot_t *snapshot)
{
    *snapshot = diagnostics_snapshot;
}

void App_ResetReason_GetSnapshot(App_ResetReason_Snapshot_t *snapshot)
{
    *snapshot = reset_snapshot;
}

static void SetRequest(const uint8_t *request, uint16_t length)
{
    (void)memcpy(request_buffer, request, length);
    request_length = length;
    request_ready = 1U;
}

static uint32_t ReadU32Be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) |
           data[3];
}

void setUp(void)
{
    (void)memset(request_buffer, 0, sizeof(request_buffer));
    (void)memset(response_buffer, 0, sizeof(response_buffer));
    request_length = 0U;
    request_ready = 0U;
    release_calls = 0U;
    response_length = 0U;
    response_now_us = 0U;
    response_calls = 0U;
    response_result = CAN_ISOTP_RESULT_OK;
    startup_snapshot = (App_Startup_Snapshot_t){0};
    diagnostics_snapshot = (App_Diagnostics_Snapshot_t){0};
    reset_snapshot = (App_ResetReason_Snapshot_t){0};
    isotp_stats = (CAN_IsoTp_Stats_t){0};
    CAN_Uds_Init();
}

void tearDown(void)
{
}

void test_Process_without_request_only_services_session_timer(void)
{
    TEST_ASSERT_EQUAL(CAN_UDS_RESULT_NO_WORK, CAN_Uds_Process(100U));
    TEST_ASSERT_EQUAL_UINT32(0U, response_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, release_calls);
}

void test_TesterPresent_response_is_released_and_sent_over_isotp(void)
{
    const uint8_t request[2U] =
        {CAN_PROTOCOL_UDS_SERVICE_TESTER_PRESENT, 0U};
    CAN_Uds_Stats_t stats;

    SetRequest(request, sizeof(request));
    TEST_ASSERT_EQUAL(CAN_UDS_RESULT_OK, CAN_Uds_Process(123U));
    TEST_ASSERT_EQUAL_UINT32(1U, release_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, response_calls);
    TEST_ASSERT_EQUAL_UINT16(2U, response_length);
    TEST_ASSERT_EQUAL_UINT8(0x7EU, response_buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(0U, response_buffer[1]);
    TEST_ASSERT_EQUAL_UINT32(123000U, response_now_us);

    CAN_Uds_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.requests_processed);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.responses_started);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.server.tester_present_requests);
}

void test_Suppressed_response_releases_request_without_isotp_tx(void)
{
    const uint8_t request[2U] =
        {CAN_PROTOCOL_UDS_SERVICE_TESTER_PRESENT,
         UDS_SUPPRESS_POSITIVE_RESPONSE_MASK};
    CAN_Uds_Stats_t stats;

    SetRequest(request, sizeof(request));
    TEST_ASSERT_EQUAL(
        CAN_UDS_RESULT_RESPONSE_SUPPRESSED,
        CAN_Uds_Process(0U));
    TEST_ASSERT_EQUAL_UINT32(1U, release_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, response_calls);
    CAN_Uds_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.responses_suppressed);
}

void test_ProtocolInfoDid_reports_generated_transport_contract(void)
{
    const uint8_t request[3U] =
        {CAN_PROTOCOL_UDS_SERVICE_READ_DATA_BY_IDENTIFIER, 0xF1U, 0x00U};

    SetRequest(request, sizeof(request));
    TEST_ASSERT_EQUAL(CAN_UDS_RESULT_OK, CAN_Uds_Process(0U));
    TEST_ASSERT_EQUAL_UINT16(12U, response_length);
    TEST_ASSERT_EQUAL_HEX8(0x62U, response_buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0xF1U, response_buffer[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, response_buffer[2]);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_UDS_VERSION, response_buffer[3]);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_VERSION, response_buffer[4]);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_LOG_VERSION, response_buffer[5]);
    TEST_ASSERT_EQUAL_HEX16(CAN_ISOTP_BUFFER_CAPACITY,
                            ((uint16_t)response_buffer[6] << 8U) |
                            response_buffer[7]);
    TEST_ASSERT_EQUAL_HEX16(CAN_PROTOCOL_DIAGNOSTIC_REQUEST_RX_ID,
                            ((uint16_t)response_buffer[8] << 8U) |
                            response_buffer[9]);
    TEST_ASSERT_EQUAL_HEX16(CAN_PROTOCOL_DIAGNOSTIC_RESPONSE_TX_ID,
                            ((uint16_t)response_buffer[10] << 8U) |
                            response_buffer[11]);
}

void test_HealthAndResetDids_serialize_real_snapshots_big_endian(void)
{
    const uint8_t startup_request[3U] =
        {CAN_PROTOCOL_UDS_SERVICE_READ_DATA_BY_IDENTIFIER, 0xF1U, 0x01U};
    const uint8_t runtime_request[3U] =
        {CAN_PROTOCOL_UDS_SERVICE_READ_DATA_BY_IDENTIFIER, 0xF1U, 0x02U};
    const uint8_t reset_request[3U] =
        {CAN_PROTOCOL_UDS_SERVICE_READ_DATA_BY_IDENTIFIER, 0xF1U, 0x03U};

    startup_snapshot.expected_mask = 0x01020304UL;
    startup_snapshot.ready_mask = 0x11121314UL;
    startup_snapshot.failed_mask = 0x21222324UL;
    startup_snapshot.first_failed_resource = 0x31323334UL;
    startup_snapshot.first_failure_result = 0x41424344UL;
    startup_snapshot.degraded = 1U;
    SetRequest(startup_request, sizeof(startup_request));
    TEST_ASSERT_EQUAL(CAN_UDS_RESULT_OK, CAN_Uds_Process(0U));
    TEST_ASSERT_EQUAL_UINT16(24U, response_length);
    TEST_ASSERT_EQUAL_HEX32(startup_snapshot.expected_mask,
                            ReadU32Be(&response_buffer[3]));
    TEST_ASSERT_EQUAL_HEX32(startup_snapshot.first_failure_result,
                            ReadU32Be(&response_buffer[19]));
    TEST_ASSERT_EQUAL_UINT8(1U, response_buffer[23]);

    diagnostics_snapshot.uptime_ms = 0x01020304UL;
    diagnostics_snapshot.latched_issue_flags = 0x11121314UL;
    diagnostics_snapshot.rejected_frames_total = 0x21222324UL;
    diagnostics_snapshot.can_rx.rx_message_lost_events = 0x31323334UL;
    diagnostics_snapshot.can_tx.queue_overflow = 0x41424344UL;
    isotp_stats.rx_protocol_errors = 0x51525354UL;
    isotp_stats.tx_transport_failures = 0x61626364UL;
    SetRequest(runtime_request, sizeof(runtime_request));
    TEST_ASSERT_EQUAL(CAN_UDS_RESULT_OK, CAN_Uds_Process(0U));
    TEST_ASSERT_EQUAL_UINT16(31U, response_length);
    TEST_ASSERT_EQUAL_HEX32(diagnostics_snapshot.uptime_ms,
                            ReadU32Be(&response_buffer[3]));
    TEST_ASSERT_EQUAL_HEX32(isotp_stats.tx_transport_failures,
                            ReadU32Be(&response_buffer[27]));

    reset_snapshot.decoded_flags = 0x01020304UL;
    reset_snapshot.raw_rsr = 0x11121314UL;
    reset_snapshot.capture_count = 0x21222324UL;
    SetRequest(reset_request, sizeof(reset_request));
    TEST_ASSERT_EQUAL(CAN_UDS_RESULT_OK, CAN_Uds_Process(0U));
    TEST_ASSERT_EQUAL_UINT16(15U, response_length);
    TEST_ASSERT_EQUAL_HEX32(reset_snapshot.raw_rsr,
                            ReadU32Be(&response_buffer[7]));
}

void test_UnknownDid_produces_negative_response(void)
{
    const uint8_t request[3U] =
        {CAN_PROTOCOL_UDS_SERVICE_READ_DATA_BY_IDENTIFIER, 0x12U, 0x34U};
    const uint8_t expected[3U] = {0x7FU, 0x22U, 0x31U};

    SetRequest(request, sizeof(request));
    TEST_ASSERT_EQUAL(CAN_UDS_RESULT_OK, CAN_Uds_Process(0U));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        expected,
        response_buffer,
        3U);
}

void test_IsoTp_response_failure_is_counted(void)
{
    const uint8_t request[2U] =
        {CAN_PROTOCOL_UDS_SERVICE_TESTER_PRESENT, 0U};
    CAN_Uds_Stats_t stats;

    response_result = CAN_ISOTP_RESULT_BUSY;
    SetRequest(request, sizeof(request));
    TEST_ASSERT_EQUAL(CAN_UDS_RESULT_ISOTP_ERROR, CAN_Uds_Process(0U));
    CAN_Uds_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.isotp_response_failures);
    TEST_ASSERT_EQUAL(CAN_ISOTP_RESULT_BUSY, stats.last_isotp_result);
}

void test_Extended_session_returns_to_default_without_new_request(void)
{
    const uint8_t request[2U] =
        {CAN_PROTOCOL_UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL,
         CAN_PROTOCOL_UDS_SESSION_EXTENDED_DIAGNOSTIC};
    CAN_Uds_Stats_t stats;

    SetRequest(request, sizeof(request));
    TEST_ASSERT_EQUAL(CAN_UDS_RESULT_OK, CAN_Uds_Process(100U));
    TEST_ASSERT_EQUAL(CAN_UDS_RESULT_NO_WORK, CAN_Uds_Process(5099U));
    CAN_Uds_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT8(
        CAN_PROTOCOL_UDS_SESSION_EXTENDED_DIAGNOSTIC,
        stats.server.current_session);
    TEST_ASSERT_EQUAL(CAN_UDS_RESULT_NO_WORK, CAN_Uds_Process(5100U));
    CAN_Uds_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_UDS_SESSION_DEFAULT,
                            stats.server.current_session);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.server.session_timeouts);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_Process_without_request_only_services_session_timer);
    RUN_TEST(test_TesterPresent_response_is_released_and_sent_over_isotp);
    RUN_TEST(test_Suppressed_response_releases_request_without_isotp_tx);
    RUN_TEST(test_ProtocolInfoDid_reports_generated_transport_contract);
    RUN_TEST(test_HealthAndResetDids_serialize_real_snapshots_big_endian);
    RUN_TEST(test_UnknownDid_produces_negative_response);
    RUN_TEST(test_IsoTp_response_failure_is_counted);
    RUN_TEST(test_Extended_session_returns_to_default_without_new_request);
    return UNITY_END();
}
