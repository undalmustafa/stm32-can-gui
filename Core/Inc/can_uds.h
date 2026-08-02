#ifndef CAN_UDS_H
#define CAN_UDS_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: none
 */

#include <stdint.h>

#include "can_isotp.h"
#include "uds_server.h"

typedef enum
{
    CAN_UDS_RESULT_OK = 0,
    CAN_UDS_RESULT_NO_WORK,
    CAN_UDS_RESULT_RESPONSE_SUPPRESSED,
    CAN_UDS_RESULT_DISPATCH_ERROR,
    CAN_UDS_RESULT_ISOTP_ERROR
} CAN_Uds_Result_t;

typedef struct
{
    uint32_t requests_processed;
    uint32_t responses_started;
    uint32_t responses_suppressed;
    uint32_t dispatch_errors;
    uint32_t isotp_response_failures;
    Uds_ServerResult_t last_server_result;
    CAN_IsoTp_Result_t last_isotp_result;
    Uds_ServerStats_t server;
} CAN_Uds_Stats_t;

void CAN_Uds_Init(void);
CAN_Uds_Result_t CAN_Uds_Process(uint32_t now_ms);
void CAN_Uds_GetStats(CAN_Uds_Stats_t *stats);

#endif /* CAN_UDS_H */
