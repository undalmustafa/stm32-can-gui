#ifndef UDS_SERVER_H
#define UDS_SERVER_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: none
 */

#include <stdint.h>

#include "can_protocol_generated.h"

#define UDS_POSITIVE_RESPONSE_OFFSET 0x40U
#define UDS_NEGATIVE_RESPONSE_SERVICE 0x7FU
#define UDS_SUPPRESS_POSITIVE_RESPONSE_MASK 0x80U
#define UDS_SUBFUNCTION_MASK 0x7FU

typedef enum
{
    UDS_NRC_NONE = 0x00U,
    UDS_NRC_SERVICE_NOT_SUPPORTED = 0x11U,
    UDS_NRC_SUBFUNCTION_NOT_SUPPORTED = 0x12U,
    UDS_NRC_INCORRECT_MESSAGE_LENGTH = 0x13U,
    UDS_NRC_RESPONSE_TOO_LONG = 0x14U,
    UDS_NRC_CONDITIONS_NOT_CORRECT = 0x22U,
    UDS_NRC_REQUEST_OUT_OF_RANGE = 0x31U
} Uds_NegativeResponseCode_t;

typedef enum
{
    UDS_DID_READ_OK = 0,
    UDS_DID_READ_NOT_SUPPORTED,
    UDS_DID_READ_CONDITIONS_NOT_CORRECT,
    UDS_DID_READ_BUFFER_TOO_SMALL
} Uds_DidReadResult_t;

typedef Uds_DidReadResult_t (*Uds_ReadDidCallback_t)(
    void *context,
    uint16_t did,
    uint8_t *data,
    uint16_t capacity,
    uint16_t *data_length
);

typedef struct
{
    Uds_ReadDidCallback_t read_did;
    void *read_did_context;
} Uds_ServerConfig_t;

typedef struct
{
    uint32_t requests;
    uint32_t positive_responses;
    uint32_t negative_responses;
    uint32_t suppressed_responses;
    uint32_t unsupported_services;
    uint32_t unsupported_subfunctions;
    uint32_t invalid_lengths;
    uint32_t unsupported_dids;
    uint32_t session_changes;
    uint32_t session_timeouts;
    uint32_t tester_present_requests;
    uint8_t current_session;
    uint8_t last_service;
    Uds_NegativeResponseCode_t last_nrc;
} Uds_ServerStats_t;

typedef enum
{
    UDS_SERVER_RESULT_OK = 0,
    UDS_SERVER_RESULT_RESPONSE_READY,
    UDS_SERVER_RESULT_NO_RESPONSE,
    UDS_SERVER_RESULT_INVALID_ARGUMENT,
    UDS_SERVER_RESULT_RESPONSE_BUFFER_TOO_SMALL
} Uds_ServerResult_t;

typedef struct
{
    Uds_ServerConfig_t config;
    Uds_ServerStats_t stats;
    uint32_t session_deadline_ms;
    uint8_t session_timer_active;
    uint8_t initialized;
} Uds_Server_t;

Uds_ServerResult_t Uds_Server_Init(
    Uds_Server_t *server,
    const Uds_ServerConfig_t *config
);

Uds_ServerResult_t Uds_Server_ProcessRequest(
    Uds_Server_t *server,
    const uint8_t *request,
    uint16_t request_length,
    uint32_t now_ms,
    uint8_t *response,
    uint16_t response_capacity,
    uint16_t *response_length
);

void Uds_Server_Tick(Uds_Server_t *server, uint32_t now_ms);
void Uds_Server_GetStats(
    const Uds_Server_t *server,
    Uds_ServerStats_t *stats
);

#endif /* UDS_SERVER_H */
