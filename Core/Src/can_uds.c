#include "can_uds.h"

#include "app_diagnostics.h"
#include "app_reset_reason.h"
#include "app_startup.h"
#include "can_protocol_generated.h"

#include <stddef.h>

static Uds_Server_t uds_server;
static uint8_t uds_response_buffer[CAN_ISOTP_BUFFER_CAPACITY];
static CAN_Uds_Stats_t uds_stats;

static void CAN_Uds_WriteU16Be(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
}

static void CAN_Uds_WriteU32Be(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static Uds_DidReadResult_t CAN_Uds_ReadProtocolInfo(
    uint8_t *data,
    uint16_t capacity,
    uint16_t *data_length
)
{
    if (capacity < 9U)
    {
        return UDS_DID_READ_BUFFER_TOO_SMALL;
    }

    data[0] = CAN_PROTOCOL_UDS_VERSION;
    data[1] = CAN_PROTOCOL_VERSION;
    data[2] = CAN_PROTOCOL_LOG_VERSION;
    CAN_Uds_WriteU16Be(&data[3], CAN_ISOTP_BUFFER_CAPACITY);
    CAN_Uds_WriteU16Be(
        &data[5],
        CAN_PROTOCOL_DIAGNOSTIC_REQUEST_RX_ID);
    CAN_Uds_WriteU16Be(
        &data[7],
        CAN_PROTOCOL_DIAGNOSTIC_RESPONSE_TX_ID);
    *data_length = 9U;
    return UDS_DID_READ_OK;
}

static Uds_DidReadResult_t CAN_Uds_ReadStartupHealth(
    uint8_t *data,
    uint16_t capacity,
    uint16_t *data_length
)
{
    App_Startup_Snapshot_t snapshot;

    if (capacity < 21U)
    {
        return UDS_DID_READ_BUFFER_TOO_SMALL;
    }

    App_Startup_GetSnapshot(&snapshot);
    CAN_Uds_WriteU32Be(&data[0], snapshot.expected_mask);
    CAN_Uds_WriteU32Be(&data[4], snapshot.ready_mask);
    CAN_Uds_WriteU32Be(&data[8], snapshot.failed_mask);
    CAN_Uds_WriteU32Be(&data[12], snapshot.first_failed_resource);
    CAN_Uds_WriteU32Be(&data[16], snapshot.first_failure_result);
    data[20] = snapshot.degraded;
    *data_length = 21U;
    return UDS_DID_READ_OK;
}

static Uds_DidReadResult_t CAN_Uds_ReadRuntimeHealth(
    uint8_t *data,
    uint16_t capacity,
    uint16_t *data_length
)
{
    App_Diagnostics_Snapshot_t diagnostics;
    CAN_IsoTp_Stats_t isotp;

    if (capacity < 28U)
    {
        return UDS_DID_READ_BUFFER_TOO_SMALL;
    }

    App_Diagnostics_GetSnapshot(&diagnostics);
    CAN_IsoTp_GetStats(&isotp);
    CAN_Uds_WriteU32Be(&data[0], diagnostics.uptime_ms);
    CAN_Uds_WriteU32Be(&data[4], diagnostics.latched_issue_flags);
    CAN_Uds_WriteU32Be(&data[8], diagnostics.rejected_frames_total);
    CAN_Uds_WriteU32Be(
        &data[12],
        diagnostics.can_rx.rx_message_lost_events);
    CAN_Uds_WriteU32Be(
        &data[16],
        diagnostics.can_tx.queue_overflow);
    CAN_Uds_WriteU32Be(&data[20], isotp.rx_protocol_errors);
    CAN_Uds_WriteU32Be(&data[24], isotp.tx_transport_failures);
    *data_length = 28U;
    return UDS_DID_READ_OK;
}

static Uds_DidReadResult_t CAN_Uds_ReadResetReason(
    uint8_t *data,
    uint16_t capacity,
    uint16_t *data_length
)
{
    App_ResetReason_Snapshot_t snapshot;

    if (capacity < 12U)
    {
        return UDS_DID_READ_BUFFER_TOO_SMALL;
    }

    App_ResetReason_GetSnapshot(&snapshot);
    CAN_Uds_WriteU32Be(&data[0], snapshot.decoded_flags);
    CAN_Uds_WriteU32Be(&data[4], snapshot.raw_rsr);
    CAN_Uds_WriteU32Be(&data[8], snapshot.capture_count);
    *data_length = 12U;
    return UDS_DID_READ_OK;
}

static Uds_DidReadResult_t CAN_Uds_ReadDid(
    void *context,
    uint16_t did,
    uint8_t *data,
    uint16_t capacity,
    uint16_t *data_length
)
{
    (void)context;

    switch (did)
    {
        case CAN_PROTOCOL_UDS_DID_PROTOCOL_INFO:
            return CAN_Uds_ReadProtocolInfo(data, capacity, data_length);

        case CAN_PROTOCOL_UDS_DID_STARTUP_HEALTH:
            return CAN_Uds_ReadStartupHealth(data, capacity, data_length);

        case CAN_PROTOCOL_UDS_DID_RUNTIME_HEALTH:
            return CAN_Uds_ReadRuntimeHealth(data, capacity, data_length);

        case CAN_PROTOCOL_UDS_DID_RESET_REASON:
            return CAN_Uds_ReadResetReason(data, capacity, data_length);

        default:
            return UDS_DID_READ_NOT_SUPPORTED;
    }
}

void CAN_Uds_Init(void)
{
    CAN_Uds_InitWithProgramming(NULL, NULL, NULL);
}

void CAN_Uds_InitWithProgramming(
    Uds_ProgrammingCallback_t programming,
    Uds_ProgrammingAbortCallback_t programming_abort,
    void *programming_context)
{
    Uds_ServerConfig_t config = {0};

    uds_stats = (CAN_Uds_Stats_t){0};
    config.read_did = CAN_Uds_ReadDid;
    config.read_did_context = NULL;
    config.programming = programming;
    config.programming_abort = programming_abort;
    config.programming_context = programming_context;
    uds_stats.last_server_result = Uds_Server_Init(&uds_server, &config);
    uds_stats.last_isotp_result = CAN_ISOTP_RESULT_NO_WORK;
}

CAN_Uds_Result_t CAN_Uds_Process(uint32_t now_ms)
{
    Uds_ServerResult_t server_result;
    CAN_IsoTp_Result_t isotp_result;
    uint16_t response_length = 0U;

    Uds_Server_Tick(&uds_server, now_ms);
    if (CAN_IsoTp_HasRequest() == 0U)
    {
        return CAN_UDS_RESULT_NO_WORK;
    }

    server_result = Uds_Server_ProcessRequest(
        &uds_server,
        CAN_IsoTp_GetRequestData(),
        CAN_IsoTp_GetRequestLength(),
        now_ms,
        uds_response_buffer,
        sizeof(uds_response_buffer),
        &response_length);
    uds_stats.requests_processed++;
    uds_stats.last_server_result = server_result;
    CAN_IsoTp_ReleaseRequest();

    if (server_result == UDS_SERVER_RESULT_NO_RESPONSE)
    {
        uds_stats.responses_suppressed++;
        return CAN_UDS_RESULT_RESPONSE_SUPPRESSED;
    }

    if (server_result != UDS_SERVER_RESULT_RESPONSE_READY)
    {
        uds_stats.dispatch_errors++;
        return CAN_UDS_RESULT_DISPATCH_ERROR;
    }

    isotp_result = CAN_IsoTp_StartResponse(
        uds_response_buffer,
        response_length,
        now_ms * 1000U);
    uds_stats.last_isotp_result = isotp_result;
    if (isotp_result != CAN_ISOTP_RESULT_OK)
    {
        uds_stats.isotp_response_failures++;
        return CAN_UDS_RESULT_ISOTP_ERROR;
    }

    uds_stats.responses_started++;
    return CAN_UDS_RESULT_OK;
}

void CAN_Uds_GetStats(CAN_Uds_Stats_t *stats)
{
    if (stats != NULL)
    {
        *stats = uds_stats;
        Uds_Server_GetStats(&uds_server, &stats->server);
    }
}
