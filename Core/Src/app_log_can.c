#include "app_log_can.h"

#include "main.h"
#include "app_log.h"
#include "can_protocol.h"
#include "can_transport.h"

#include <stddef.h>

#define APP_LOG_CAN_INFO_FRAGMENT_COUNT  3U
#define APP_LOG_CAN_RECORD_FRAGMENT_COUNT 5U

static App_Log_Can_Stats_t app_log_can_stats;
static uint32_t app_log_can_next_heartbeat_tick;
static uint8_t app_log_can_alive_counter;

static uint8_t App_Log_Can_SendFragments(uint8_t fragment_base,
                                         const uint8_t *bytes,
                                         uint32_t byte_count,
                                         uint8_t fragment_count)
{
    uint8_t fragment_index;
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE];
    uint32_t source_index;
    uint8_t payload_index;
    CAN_Transport_Result_t result;

    for (fragment_index = 0U;
         fragment_index < fragment_count;
         fragment_index++)
    {
        payload[0] = (uint8_t)(fragment_base | fragment_index);

        for (payload_index = 1U;
             payload_index < CAN_PROTOCOL_PAYLOAD_SIZE;
             payload_index++)
        {
            source_index =
                ((uint32_t)fragment_index *
                 CAN_PROTOCOL_LOG_FRAGMENT_DATA_SIZE) +
                ((uint32_t)payload_index - 1U);

            payload[payload_index] =
                (source_index < byte_count) ? bytes[source_index] : 0U;
        }

        result = CAN_Transport_SendClassicHighPriority(
            CAN_PROTOCOL_LOG_RESPONSE_TX_ID,
            CAN_TRANSPORT_ID_STANDARD,
            payload);

        if ((result != CAN_TRANSPORT_OK) &&
            (result != CAN_TRANSPORT_QUEUED))
        {
            app_log_can_stats.tx_failures++;
            return 0U;
        }

        app_log_can_stats.fragments_sent++;
    }

    return 1U;
}

static void App_Log_Can_WriteU16LE(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void App_Log_Can_SendNotFound(uint32_t sequence)
{
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE] = {0};
    CAN_Transport_Result_t result;

    payload[0] = CAN_PROTOCOL_LOG_ERROR_FRAME;
    payload[1] = CAN_PROTOCOL_CMD_LOG_READ_SEQUENCE;
    payload[2] = CAN_PROTOCOL_LOG_ERROR_SEQUENCE_NOT_FOUND;
    CAN_Protocol_WriteU32LE(&payload[4], sequence);

    result = CAN_Transport_SendClassicHighPriority(
        CAN_PROTOCOL_LOG_RESPONSE_TX_ID,
        CAN_TRANSPORT_ID_STANDARD,
        payload);

    if ((result == CAN_TRANSPORT_OK) ||
        (result == CAN_TRANSPORT_QUEUED))
    {
        app_log_can_stats.fragments_sent++;
    }
    else
    {
        app_log_can_stats.tx_failures++;
    }
}

void App_Log_Can_Init(void)
{
    app_log_can_stats = (App_Log_Can_Stats_t){0};
    app_log_can_next_heartbeat_tick = HAL_GetTick();
    app_log_can_alive_counter = 0U;
}

void App_Log_Can_Process(void)
{
    App_Log_Info_t info;
    CAN_Protocol_LogHeartbeat_t heartbeat;
    CAN_Transport_Result_t result;
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE] = {0};
    uint32_t now = HAL_GetTick();
    uint32_t missed_periods = 0U;

    if ((int32_t)(now - app_log_can_next_heartbeat_tick) < 0)
    {
        return;
    }

    /*
     * Advance from the previous deadline, not from 'now'. This prevents
     * long-term drift. If the main loop was delayed, skip stale periods
     * instead of generating a burst of old heartbeat frames.
     */
    do
    {
        app_log_can_next_heartbeat_tick +=
            APP_LOG_CAN_HEARTBEAT_PERIOD_MS;

        if ((int32_t)(now - app_log_can_next_heartbeat_tick) >= 0)
        {
            missed_periods++;
        }
    }
    while ((int32_t)(now - app_log_can_next_heartbeat_tick) >= 0);

    app_log_can_alive_counter = (uint8_t)(
        app_log_can_alive_counter + (uint8_t)missed_periods);

    App_Log_GetInfo(&info);

    heartbeat.latest_sequence = info.latest_sequence;
    heartbeat.record_count = (uint8_t)info.count;
    heartbeat.ready = 1U;
    heartbeat.overwrite_detected =
        (info.overwrite_count != 0U) ? 1U : 0U;
    heartbeat.alive_counter = app_log_can_alive_counter;

    CAN_Protocol_EncodeLogHeartbeat(&heartbeat, payload);

    app_log_can_stats.heartbeat_attempts++;
    app_log_can_stats.heartbeat_missed_periods += missed_periods;
    app_log_can_stats.last_heartbeat_tick = now;
    app_log_can_stats.last_heartbeat_alive_counter =
        app_log_can_alive_counter;
    app_log_can_alive_counter++;

    result = CAN_Transport_SendClassicLatest(
        CAN_PROTOCOL_LOG_HEARTBEAT_TX_ID,
        CAN_TRANSPORT_ID_STANDARD,
        payload);

    if ((result == CAN_TRANSPORT_OK) ||
        (result == CAN_TRANSPORT_QUEUED))
    {
        app_log_can_stats.heartbeats_sent++;
    }
    else
    {
        app_log_can_stats.tx_failures++;
    }
}

void App_Log_Can_SendInfo(void)
{
    App_Log_Info_t info;
    uint8_t wire_info[CAN_PROTOCOL_LOG_INFO_WIRE_SIZE] = {0};

    app_log_can_stats.info_requests++;
    App_Log_GetInfo(&info);

    wire_info[0] = CAN_PROTOCOL_LOG_VERSION;
    wire_info[1] = (uint8_t)info.record_size;
    App_Log_Can_WriteU16LE(&wire_info[2], info.count);
    App_Log_Can_WriteU16LE(&wire_info[4], info.capacity);
    CAN_Protocol_WriteU32LE(&wire_info[6], info.oldest_sequence);
    CAN_Protocol_WriteU32LE(&wire_info[10], info.latest_sequence);
    CAN_Protocol_WriteU32LE(&wire_info[14], info.overwrite_count);

    if (App_Log_Can_SendFragments(
            CAN_PROTOCOL_LOG_INFO_FRAGMENT_BASE,
            wire_info,
            sizeof(wire_info),
            APP_LOG_CAN_INFO_FRAGMENT_COUNT) != 0U)
    {
        app_log_can_stats.info_responses++;
    }
}

void App_Log_Can_SendRecord(uint32_t sequence)
{
    App_Log_Record_t record;

    app_log_can_stats.record_requests++;
    app_log_can_stats.last_requested_sequence = sequence;

    if (App_Log_ReadBySequence(sequence, &record) == 0U)
    {
        app_log_can_stats.records_not_found++;
        App_Log_Can_SendNotFound(sequence);
        return;
    }

    if (App_Log_Can_SendFragments(
            CAN_PROTOCOL_LOG_RECORD_FRAGMENT_BASE,
            (const uint8_t *)&record,
            sizeof(record),
            APP_LOG_CAN_RECORD_FRAGMENT_COUNT) != 0U)
    {
        app_log_can_stats.records_sent++;
    }
}

void App_Log_Can_GetStats(App_Log_Can_Stats_t *stats)
{
    if (stats != NULL)
    {
        *stats = app_log_can_stats;
    }
}
