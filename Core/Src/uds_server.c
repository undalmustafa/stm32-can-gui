#include "uds_server.h"

#include <stddef.h>

static uint8_t Uds_DeadlineReached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (uint8_t)(((int32_t)(now_ms - deadline_ms)) >= 0);
}

static void Uds_WriteU16Be(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
}

static void Uds_RefreshSessionTimer(Uds_Server_t *server, uint32_t now_ms)
{
    if (server->stats.current_session != CAN_PROTOCOL_UDS_SESSION_DEFAULT)
    {
        server->session_deadline_ms =
            now_ms + CAN_PROTOCOL_UDS_S3_SERVER_TIMEOUT_MS;
        server->session_timer_active = 1U;
    }
}

static void Uds_AbortProgramming(Uds_Server_t *server)
{
    if (server->config.programming_abort != NULL)
    {
        server->config.programming_abort(server->config.programming_context);
    }
    server->stats.programming_aborts++;
}

static Uds_ServerResult_t Uds_NegativeResponse(
    Uds_Server_t *server,
    uint8_t service,
    Uds_NegativeResponseCode_t nrc,
    uint8_t *response,
    uint16_t response_capacity,
    uint16_t *response_length
)
{
    if (response_capacity < 3U)
    {
        return UDS_SERVER_RESULT_RESPONSE_BUFFER_TOO_SMALL;
    }

    response[0] = UDS_NEGATIVE_RESPONSE_SERVICE;
    response[1] = service;
    response[2] = (uint8_t)nrc;
    *response_length = 3U;
    server->stats.negative_responses++;
    server->stats.last_nrc = nrc;
    return UDS_SERVER_RESULT_RESPONSE_READY;
}

static Uds_ServerResult_t Uds_ProcessSessionControl(
    Uds_Server_t *server,
    const uint8_t *request,
    uint16_t request_length,
    uint32_t now_ms,
    uint8_t *response,
    uint16_t response_capacity,
    uint16_t *response_length
)
{
    uint8_t requested_session;
    uint8_t suppress_response;
    uint16_t p2_star_units;

    if (request_length != 2U)
    {
        server->stats.invalid_lengths++;
        return Uds_NegativeResponse(
            server,
            request[0],
            UDS_NRC_INCORRECT_MESSAGE_LENGTH,
            response,
            response_capacity,
            response_length);
    }

    suppress_response = (uint8_t)(
        request[1] & UDS_SUPPRESS_POSITIVE_RESPONSE_MASK);
    requested_session = (uint8_t)(request[1] & UDS_SUBFUNCTION_MASK);
    if ((requested_session != CAN_PROTOCOL_UDS_SESSION_DEFAULT) &&
        (requested_session != CAN_PROTOCOL_UDS_SESSION_PROGRAMMING) &&
        (requested_session !=
         CAN_PROTOCOL_UDS_SESSION_EXTENDED_DIAGNOSTIC))
    {
        server->stats.unsupported_subfunctions++;
        return Uds_NegativeResponse(
            server,
            request[0],
            UDS_NRC_SUBFUNCTION_NOT_SUPPORTED,
            response,
            response_capacity,
            response_length);
    }

    if (server->stats.current_session != requested_session)
    {
        if ((server->stats.current_session ==
             CAN_PROTOCOL_UDS_SESSION_PROGRAMMING) &&
            (requested_session != CAN_PROTOCOL_UDS_SESSION_PROGRAMMING))
        {
            Uds_AbortProgramming(server);
        }
        server->stats.session_changes++;
    }
    server->stats.current_session = requested_session;
    if (requested_session == CAN_PROTOCOL_UDS_SESSION_DEFAULT)
    {
        server->session_timer_active = 0U;
    }
    else
    {
        Uds_RefreshSessionTimer(server, now_ms);
    }

    if (suppress_response != 0U)
    {
        server->stats.suppressed_responses++;
        return UDS_SERVER_RESULT_NO_RESPONSE;
    }

    if (response_capacity < 6U)
    {
        return UDS_SERVER_RESULT_RESPONSE_BUFFER_TOO_SMALL;
    }

    p2_star_units = (uint16_t)(
        CAN_PROTOCOL_UDS_P2_STAR_SERVER_MAX_MS / 10U);
    response[0] = (uint8_t)(
        CAN_PROTOCOL_UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL +
        UDS_POSITIVE_RESPONSE_OFFSET);
    response[1] = requested_session;
    Uds_WriteU16Be(
        &response[2],
        CAN_PROTOCOL_UDS_P2_SERVER_MAX_MS);
    Uds_WriteU16Be(&response[4], p2_star_units);
    *response_length = 6U;
    server->stats.positive_responses++;
    server->stats.last_nrc = UDS_NRC_NONE;
    return UDS_SERVER_RESULT_RESPONSE_READY;
}

static Uds_ServerResult_t Uds_ProcessTesterPresent(
    Uds_Server_t *server,
    const uint8_t *request,
    uint16_t request_length,
    uint8_t *response,
    uint16_t response_capacity,
    uint16_t *response_length
)
{
    uint8_t subfunction;
    uint8_t suppress_response;

    if (request_length != 2U)
    {
        server->stats.invalid_lengths++;
        return Uds_NegativeResponse(
            server,
            request[0],
            UDS_NRC_INCORRECT_MESSAGE_LENGTH,
            response,
            response_capacity,
            response_length);
    }

    suppress_response = (uint8_t)(
        request[1] & UDS_SUPPRESS_POSITIVE_RESPONSE_MASK);
    subfunction = (uint8_t)(request[1] & UDS_SUBFUNCTION_MASK);
    if (subfunction != 0U)
    {
        server->stats.unsupported_subfunctions++;
        return Uds_NegativeResponse(
            server,
            request[0],
            UDS_NRC_SUBFUNCTION_NOT_SUPPORTED,
            response,
            response_capacity,
            response_length);
    }

    server->stats.tester_present_requests++;
    if (suppress_response != 0U)
    {
        server->stats.suppressed_responses++;
        return UDS_SERVER_RESULT_NO_RESPONSE;
    }

    if (response_capacity < 2U)
    {
        return UDS_SERVER_RESULT_RESPONSE_BUFFER_TOO_SMALL;
    }

    response[0] = (uint8_t)(
        CAN_PROTOCOL_UDS_SERVICE_TESTER_PRESENT +
        UDS_POSITIVE_RESPONSE_OFFSET);
    response[1] = 0U;
    *response_length = 2U;
    server->stats.positive_responses++;
    server->stats.last_nrc = UDS_NRC_NONE;
    return UDS_SERVER_RESULT_RESPONSE_READY;
}

static Uds_ServerResult_t Uds_ProcessReadDid(
    Uds_Server_t *server,
    const uint8_t *request,
    uint16_t request_length,
    uint8_t *response,
    uint16_t response_capacity,
    uint16_t *response_length
)
{
    uint16_t request_offset;
    uint16_t response_offset = 1U;

    if ((request_length < 3U) || ((request_length & 1U) == 0U))
    {
        server->stats.invalid_lengths++;
        return Uds_NegativeResponse(
            server,
            request[0],
            UDS_NRC_INCORRECT_MESSAGE_LENGTH,
            response,
            response_capacity,
            response_length);
    }

    if (response_capacity < 1U)
    {
        return UDS_SERVER_RESULT_RESPONSE_BUFFER_TOO_SMALL;
    }
    response[0] = (uint8_t)(
        CAN_PROTOCOL_UDS_SERVICE_READ_DATA_BY_IDENTIFIER +
        UDS_POSITIVE_RESPONSE_OFFSET);

    for (request_offset = 1U;
         request_offset < request_length;
         request_offset = (uint16_t)(request_offset + 2U))
    {
        uint16_t did = (uint16_t)(
            ((uint16_t)request[request_offset] << 8U) |
            request[request_offset + 1U]);
        uint16_t data_length = 0U;
        Uds_DidReadResult_t did_result;

        if ((server->config.read_did == NULL) ||
            ((uint16_t)(response_capacity - response_offset) < 2U))
        {
            did_result = (server->config.read_did == NULL)
                       ? UDS_DID_READ_NOT_SUPPORTED
                       : UDS_DID_READ_BUFFER_TOO_SMALL;
        }
        else
        {
            Uds_WriteU16Be(&response[response_offset], did);
            response_offset = (uint16_t)(response_offset + 2U);
            did_result = server->config.read_did(
                server->config.read_did_context,
                did,
                &response[response_offset],
                (uint16_t)(response_capacity - response_offset),
                &data_length);
            if ((did_result == UDS_DID_READ_OK) &&
                (data_length >
                 (uint16_t)(response_capacity - response_offset)))
            {
                did_result = UDS_DID_READ_BUFFER_TOO_SMALL;
            }
        }

        if (did_result != UDS_DID_READ_OK)
        {
            if (did_result == UDS_DID_READ_NOT_SUPPORTED)
            {
                server->stats.unsupported_dids++;
                return Uds_NegativeResponse(
                    server,
                    request[0],
                    UDS_NRC_REQUEST_OUT_OF_RANGE,
                    response,
                    response_capacity,
                    response_length);
            }
            if (did_result == UDS_DID_READ_BUFFER_TOO_SMALL)
            {
                return Uds_NegativeResponse(
                    server,
                    request[0],
                    UDS_NRC_RESPONSE_TOO_LONG,
                    response,
                    response_capacity,
                    response_length);
            }
            return Uds_NegativeResponse(
                server,
                request[0],
                UDS_NRC_CONDITIONS_NOT_CORRECT,
                response,
                response_capacity,
                response_length);
        }

        response_offset = (uint16_t)(response_offset + data_length);
    }

    *response_length = response_offset;
    server->stats.positive_responses++;
    server->stats.last_nrc = UDS_NRC_NONE;
    return UDS_SERVER_RESULT_RESPONSE_READY;
}

static Uds_ServerResult_t Uds_ProcessProgramming(
    Uds_Server_t *server,
    const uint8_t *request,
    uint16_t request_length,
    uint8_t *response,
    uint16_t response_capacity,
    uint16_t *response_length)
{
    Uds_NegativeResponseCode_t nrc;
    uint16_t response_data_length = 0U;

    if (server->stats.current_session !=
        CAN_PROTOCOL_UDS_SESSION_PROGRAMMING)
    {
        return Uds_NegativeResponse(
            server,
            request[0],
            UDS_NRC_SERVICE_NOT_SUPPORTED_IN_ACTIVE_SESSION,
            response,
            response_capacity,
            response_length);
    }
    if (server->config.programming == NULL)
    {
        return Uds_NegativeResponse(
            server,
            request[0],
            UDS_NRC_CONDITIONS_NOT_CORRECT,
            response,
            response_capacity,
            response_length);
    }
    if (response_capacity < 1U)
    {
        return UDS_SERVER_RESULT_RESPONSE_BUFFER_TOO_SMALL;
    }

    server->stats.programming_requests++;
    nrc = server->config.programming(
        server->config.programming_context,
        request[0],
        request,
        request_length,
        &response[1],
        (uint16_t)(response_capacity - 1U),
        &response_data_length);
    if (nrc != UDS_NRC_NONE)
    {
        return Uds_NegativeResponse(
            server,
            request[0],
            nrc,
            response,
            response_capacity,
            response_length);
    }
    if (response_data_length > (uint16_t)(response_capacity - 1U))
    {
        return Uds_NegativeResponse(
            server,
            request[0],
            UDS_NRC_RESPONSE_TOO_LONG,
            response,
            response_capacity,
            response_length);
    }

    response[0] = (uint8_t)(request[0] + UDS_POSITIVE_RESPONSE_OFFSET);
    *response_length = (uint16_t)(response_data_length + 1U);
    server->stats.positive_responses++;
    server->stats.last_nrc = UDS_NRC_NONE;
    return UDS_SERVER_RESULT_RESPONSE_READY;
}

Uds_ServerResult_t Uds_Server_Init(
    Uds_Server_t *server,
    const Uds_ServerConfig_t *config
)
{
    if ((server == NULL) || (config == NULL))
    {
        return UDS_SERVER_RESULT_INVALID_ARGUMENT;
    }

    *server = (Uds_Server_t){0};
    server->config = *config;
    server->stats.current_session = CAN_PROTOCOL_UDS_SESSION_DEFAULT;
    server->initialized = 1U;
    return UDS_SERVER_RESULT_OK;
}

Uds_ServerResult_t Uds_Server_ProcessRequest(
    Uds_Server_t *server,
    const uint8_t *request,
    uint16_t request_length,
    uint32_t now_ms,
    uint8_t *response,
    uint16_t response_capacity,
    uint16_t *response_length
)
{
    if ((server == NULL) || (request == NULL) || (response == NULL) ||
        (response_length == NULL) || (request_length == 0U) ||
        (server->initialized == 0U))
    {
        return UDS_SERVER_RESULT_INVALID_ARGUMENT;
    }

    *response_length = 0U;
    Uds_Server_Tick(server, now_ms);
    Uds_RefreshSessionTimer(server, now_ms);
    server->stats.requests++;
    server->stats.last_service = request[0];

    switch (request[0])
    {
        case CAN_PROTOCOL_UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL:
            return Uds_ProcessSessionControl(
                server,
                request,
                request_length,
                now_ms,
                response,
                response_capacity,
                response_length);

        case CAN_PROTOCOL_UDS_SERVICE_READ_DATA_BY_IDENTIFIER:
            return Uds_ProcessReadDid(
                server,
                request,
                request_length,
                response,
                response_capacity,
                response_length);

        case CAN_PROTOCOL_UDS_SERVICE_TESTER_PRESENT:
            return Uds_ProcessTesterPresent(
                server,
                request,
                request_length,
                response,
                response_capacity,
                response_length);

        case CAN_PROTOCOL_UDS_SERVICE_ROUTINE_CONTROL:
        case CAN_PROTOCOL_UDS_SERVICE_REQUEST_DOWNLOAD:
        case CAN_PROTOCOL_UDS_SERVICE_TRANSFER_DATA:
        case CAN_PROTOCOL_UDS_SERVICE_REQUEST_TRANSFER_EXIT:
            return Uds_ProcessProgramming(
                server,
                request,
                request_length,
                response,
                response_capacity,
                response_length);

        default:
            server->stats.unsupported_services++;
            return Uds_NegativeResponse(
                server,
                request[0],
                UDS_NRC_SERVICE_NOT_SUPPORTED,
                response,
                response_capacity,
                response_length);
    }
}

void Uds_Server_Tick(Uds_Server_t *server, uint32_t now_ms)
{
    if ((server == NULL) || (server->initialized == 0U) ||
        (server->session_timer_active == 0U))
    {
        return;
    }

    if (Uds_DeadlineReached(now_ms, server->session_deadline_ms) != 0U)
    {
        if (server->stats.current_session ==
            CAN_PROTOCOL_UDS_SESSION_PROGRAMMING)
        {
            Uds_AbortProgramming(server);
        }
        server->stats.current_session = CAN_PROTOCOL_UDS_SESSION_DEFAULT;
        server->stats.session_timeouts++;
        server->session_timer_active = 0U;
    }
}

void Uds_Server_GetStats(
    const Uds_Server_t *server,
    Uds_ServerStats_t *stats
)
{
    if ((server != NULL) && (stats != NULL))
    {
        *stats = server->stats;
    }
}
