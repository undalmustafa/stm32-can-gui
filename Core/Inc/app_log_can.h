#ifndef APP_LOG_CAN_H
#define APP_LOG_CAN_H

#include <stdint.h>

#define APP_LOG_CAN_HEARTBEAT_PERIOD_MS  100U

typedef struct
{
    uint32_t info_requests;
    uint32_t record_requests;
    uint32_t info_responses;
    uint32_t records_sent;
    uint32_t records_not_found;
    uint32_t fragments_sent;
    uint32_t tx_failures;
    uint32_t last_requested_sequence;
    uint32_t heartbeat_attempts;
    uint32_t heartbeats_sent;
    uint32_t heartbeat_missed_periods;
    uint32_t last_heartbeat_tick;
    uint8_t last_heartbeat_alive_counter;
} App_Log_Can_Stats_t;

void App_Log_Can_Init(void);
void App_Log_Can_SendInfo(void);
void App_Log_Can_SendRecord(uint32_t sequence);
void App_Log_Can_Process(void);
void App_Log_Can_GetStats(App_Log_Can_Stats_t *stats);

#endif /* APP_LOG_CAN_H */
