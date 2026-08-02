#include "unity.h"

#include "uds_server.h"

#include <stddef.h>

static Uds_Server_t server;
static Uds_ServerConfig_t config;
static uint8_t response[64U];
static uint16_t response_length;
static uint32_t did_read_calls;

static Uds_DidReadResult_t ReadDid(
    void *context,
    uint16_t did,
    uint8_t *data,
    uint16_t capacity,
    uint16_t *data_length)
{
    TEST_ASSERT_EQUAL_PTR(&did_read_calls, context);
    did_read_calls++;

    if (did == CAN_PROTOCOL_UDS_DID_PROTOCOL_INFO)
    {
        if (capacity < 3U)
        {
            return UDS_DID_READ_BUFFER_TOO_SMALL;
        }
        data[0] = 1U;
        data[1] = 2U;
        data[2] = 3U;
        *data_length = 3U;
        return UDS_DID_READ_OK;
    }

    if (did == CAN_PROTOCOL_UDS_DID_STARTUP_HEALTH)
    {
        if (capacity < 2U)
        {
            return UDS_DID_READ_BUFFER_TOO_SMALL;
        }
        data[0] = 0xAAU;
        data[1] = 0x55U;
        *data_length = 2U;
        return UDS_DID_READ_OK;
    }

    return UDS_DID_READ_NOT_SUPPORTED;
}

static Uds_ServerResult_t Process(
    const uint8_t *request,
    uint16_t request_length,
    uint32_t now_ms)
{
    return Uds_Server_ProcessRequest(
        &server,
        request,
        request_length,
        now_ms,
        response,
        sizeof(response),
        &response_length);
}

void setUp(void)
{
    uint16_t index;

    did_read_calls = 0U;
    response_length = 0U;
    for (index = 0U; index < sizeof(response); index++)
    {
        response[index] = 0U;
    }
    config.read_did = ReadDid;
    config.read_did_context = &did_read_calls;
    TEST_ASSERT_EQUAL(UDS_SERVER_RESULT_OK, Uds_Server_Init(&server, &config));
}

void tearDown(void)
{
}

void test_DefaultSession_response_contains_standard_timing(void)
{
    const uint8_t request[2U] =
        {CAN_PROTOCOL_UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL,
         CAN_PROTOCOL_UDS_SESSION_DEFAULT};
    Uds_ServerStats_t stats;

    TEST_ASSERT_EQUAL(
        UDS_SERVER_RESULT_RESPONSE_READY,
        Process(request, sizeof(request), 100U));
    TEST_ASSERT_EQUAL_UINT16(6U, response_length);
    TEST_ASSERT_EQUAL_HEX8(0x50U, response[0]);
    TEST_ASSERT_EQUAL_HEX8(CAN_PROTOCOL_UDS_SESSION_DEFAULT, response[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, response[2]);
    TEST_ASSERT_EQUAL_HEX8(0x32U, response[3]);
    TEST_ASSERT_EQUAL_HEX8(0x01U, response[4]);
    TEST_ASSERT_EQUAL_HEX8(0xF4U, response[5]);

    Uds_Server_GetStats(&server, &stats);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_UDS_SESSION_DEFAULT,
                            stats.current_session);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.positive_responses);
}

void test_ExtendedSession_suppression_and_timeout_are_wrap_safe(void)
{
    const uint8_t request[2U] =
        {CAN_PROTOCOL_UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL,
         (uint8_t)(UDS_SUPPRESS_POSITIVE_RESPONSE_MASK |
                   CAN_PROTOCOL_UDS_SESSION_EXTENDED_DIAGNOSTIC)};
    Uds_ServerStats_t stats;

    TEST_ASSERT_EQUAL(
        UDS_SERVER_RESULT_NO_RESPONSE,
        Process(request, sizeof(request), UINT32_MAX - 1000U));
    TEST_ASSERT_EQUAL_UINT16(0U, response_length);
    Uds_Server_GetStats(&server, &stats);
    TEST_ASSERT_EQUAL_UINT8(
        CAN_PROTOCOL_UDS_SESSION_EXTENDED_DIAGNOSTIC,
        stats.current_session);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.suppressed_responses);

    Uds_Server_Tick(&server, 3998U);
    Uds_Server_GetStats(&server, &stats);
    TEST_ASSERT_EQUAL_UINT8(
        CAN_PROTOCOL_UDS_SESSION_EXTENDED_DIAGNOSTIC,
        stats.current_session);
    Uds_Server_Tick(&server, 3999U);
    Uds_Server_GetStats(&server, &stats);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_UDS_SESSION_DEFAULT,
                            stats.current_session);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.session_timeouts);
}

void test_TesterPresent_refreshes_extended_session_timer(void)
{
    const uint8_t extended[2U] =
        {CAN_PROTOCOL_UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL,
         CAN_PROTOCOL_UDS_SESSION_EXTENDED_DIAGNOSTIC};
    const uint8_t tester_present[2U] =
        {CAN_PROTOCOL_UDS_SERVICE_TESTER_PRESENT, 0U};
    Uds_ServerStats_t stats;

    TEST_ASSERT_EQUAL(
        UDS_SERVER_RESULT_RESPONSE_READY,
        Process(extended, sizeof(extended), 0U));
    TEST_ASSERT_EQUAL(
        UDS_SERVER_RESULT_RESPONSE_READY,
        Process(tester_present, sizeof(tester_present), 4000U));
    TEST_ASSERT_EQUAL_UINT16(2U, response_length);
    TEST_ASSERT_EQUAL_HEX8(0x7EU, response[0]);
    TEST_ASSERT_EQUAL_HEX8(0U, response[1]);

    Uds_Server_Tick(&server, 8999U);
    Uds_Server_GetStats(&server, &stats);
    TEST_ASSERT_EQUAL_UINT8(
        CAN_PROTOCOL_UDS_SESSION_EXTENDED_DIAGNOSTIC,
        stats.current_session);
    Uds_Server_Tick(&server, 9000U);
    Uds_Server_GetStats(&server, &stats);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_UDS_SESSION_DEFAULT,
                            stats.current_session);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.tester_present_requests);
}

void test_TesterPresent_supports_suppress_positive_response(void)
{
    const uint8_t request[2U] =
        {CAN_PROTOCOL_UDS_SERVICE_TESTER_PRESENT,
         UDS_SUPPRESS_POSITIVE_RESPONSE_MASK};

    TEST_ASSERT_EQUAL(
        UDS_SERVER_RESULT_NO_RESPONSE,
        Process(request, sizeof(request), 0U));
    TEST_ASSERT_EQUAL_UINT16(0U, response_length);
}

void test_SessionAndTesterPresent_validate_length_and_subfunction(void)
{
    const uint8_t short_session[1U] =
        {CAN_PROTOCOL_UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL};
    const uint8_t invalid_tester[2U] =
        {CAN_PROTOCOL_UDS_SERVICE_TESTER_PRESENT, 1U};
    Uds_ServerStats_t stats;

    TEST_ASSERT_EQUAL(
        UDS_SERVER_RESULT_RESPONSE_READY,
        Process(short_session, sizeof(short_session), 0U));
    TEST_ASSERT_EQUAL_UINT8(UDS_NEGATIVE_RESPONSE_SERVICE, response[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_NRC_INCORRECT_MESSAGE_LENGTH, response[2]);

    TEST_ASSERT_EQUAL(
        UDS_SERVER_RESULT_RESPONSE_READY,
        Process(invalid_tester, sizeof(invalid_tester), 0U));
    TEST_ASSERT_EQUAL_UINT8(UDS_NRC_SUBFUNCTION_NOT_SUPPORTED, response[2]);

    Uds_Server_GetStats(&server, &stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.invalid_lengths);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.unsupported_subfunctions);
    TEST_ASSERT_EQUAL_UINT32(2U, stats.negative_responses);
}

void test_ReadDid_returns_one_supported_identifier(void)
{
    const uint8_t request[3U] =
        {CAN_PROTOCOL_UDS_SERVICE_READ_DATA_BY_IDENTIFIER, 0xF1U, 0x00U};

    TEST_ASSERT_EQUAL(
        UDS_SERVER_RESULT_RESPONSE_READY,
        Process(request, sizeof(request), 0U));
    TEST_ASSERT_EQUAL_UINT16(6U, response_length);
    TEST_ASSERT_EQUAL_HEX8(0x62U, response[0]);
    TEST_ASSERT_EQUAL_HEX8(0xF1U, response[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, response[2]);
    TEST_ASSERT_EQUAL_UINT8(1U, response[3]);
    TEST_ASSERT_EQUAL_UINT8(2U, response[4]);
    TEST_ASSERT_EQUAL_UINT8(3U, response[5]);
}

void test_ReadDid_supports_multiple_identifiers_in_order(void)
{
    const uint8_t request[5U] =
        {CAN_PROTOCOL_UDS_SERVICE_READ_DATA_BY_IDENTIFIER,
         0xF1U, 0x00U, 0xF1U, 0x01U};
    const uint8_t expected[10U] =
        {0x62U, 0xF1U, 0x00U, 1U, 2U, 3U,
         0xF1U, 0x01U, 0xAAU, 0x55U};

    TEST_ASSERT_EQUAL(
        UDS_SERVER_RESULT_RESPONSE_READY,
        Process(request, sizeof(request), 0U));
    TEST_ASSERT_EQUAL_UINT16(10U, response_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        expected,
        response,
        10U);
    TEST_ASSERT_EQUAL_UINT32(2U, did_read_calls);
}

void test_ReadDid_rejects_bad_length_and_unknown_identifier(void)
{
    const uint8_t bad_length[2U] =
        {CAN_PROTOCOL_UDS_SERVICE_READ_DATA_BY_IDENTIFIER, 0xF1U};
    const uint8_t unknown[3U] =
        {CAN_PROTOCOL_UDS_SERVICE_READ_DATA_BY_IDENTIFIER, 0x12U, 0x34U};
    Uds_ServerStats_t stats;

    TEST_ASSERT_EQUAL(
        UDS_SERVER_RESULT_RESPONSE_READY,
        Process(bad_length, sizeof(bad_length), 0U));
    TEST_ASSERT_EQUAL_UINT8(UDS_NRC_INCORRECT_MESSAGE_LENGTH, response[2]);
    TEST_ASSERT_EQUAL(
        UDS_SERVER_RESULT_RESPONSE_READY,
        Process(unknown, sizeof(unknown), 0U));
    TEST_ASSERT_EQUAL_UINT8(UDS_NRC_REQUEST_OUT_OF_RANGE, response[2]);

    Uds_Server_GetStats(&server, &stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.unsupported_dids);
}

void test_Unsupported_service_returns_service_not_supported(void)
{
    const uint8_t request[1U] = {0x99U};
    const uint8_t expected[3U] = {0x7FU, 0x99U, 0x11U};

    TEST_ASSERT_EQUAL(
        UDS_SERVER_RESULT_RESPONSE_READY,
        Process(request, sizeof(request), 0U));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        expected,
        response,
        3U);
}

void test_Response_capacity_errors_do_not_overwrite_length(void)
{
    const uint8_t request[2U] =
        {CAN_PROTOCOL_UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL,
         CAN_PROTOCOL_UDS_SESSION_DEFAULT};

    response_length = 55U;
    TEST_ASSERT_EQUAL(
        UDS_SERVER_RESULT_RESPONSE_BUFFER_TOO_SMALL,
        Uds_Server_ProcessRequest(
            &server,
            request,
            sizeof(request),
            0U,
            response,
            5U,
            &response_length));
    TEST_ASSERT_EQUAL_UINT16(0U, response_length);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_DefaultSession_response_contains_standard_timing);
    RUN_TEST(test_ExtendedSession_suppression_and_timeout_are_wrap_safe);
    RUN_TEST(test_TesterPresent_refreshes_extended_session_timer);
    RUN_TEST(test_TesterPresent_supports_suppress_positive_response);
    RUN_TEST(
        test_SessionAndTesterPresent_validate_length_and_subfunction);
    RUN_TEST(test_ReadDid_returns_one_supported_identifier);
    RUN_TEST(test_ReadDid_supports_multiple_identifiers_in_order);
    RUN_TEST(test_ReadDid_rejects_bad_length_and_unknown_identifier);
    RUN_TEST(test_Unsupported_service_returns_service_not_supported);
    RUN_TEST(test_Response_capacity_errors_do_not_overwrite_length);
    return UNITY_END();
}
