#include "main.h"
#include "app_diagnostics.h"
#include "app_log.h"

#define APP_DIAGNOSTICS_UPDATE_PERIOD_MS 100U

static App_Diagnostics_Snapshot_t diagnostics_snapshot;
static uint32_t last_diagnostics_update_tick;
static uint32_t last_logged_queue_overflow_count;
static uint32_t last_logged_rx_budget_hit_count;
static uint32_t last_logged_rx_watermark_count;
static uint32_t last_logged_rx_full_count;
static uint32_t last_logged_rx_message_lost_count;
static uint32_t
    last_logged_timing_overrun_count[APP_TIMING_SERVICE_COUNT];

static void App_Diagnostics_LogCounterDelta(
    uint32_t current_count,
    uint32_t *last_logged_count,
    App_Log_Severity_t severity,
    App_Log_EventCode_t event_code)
{
    if (current_count < *last_logged_count)
    {
        *last_logged_count = current_count;
    }
    else if (current_count > *last_logged_count)
    {
        uint32_t delta = current_count - *last_logged_count;

        (void)App_Log_Push(APP_LOG_SOURCE_CAN,
                           severity,
                           event_code,
                           delta,
                           current_count);
        *last_logged_count = current_count;
    }
}

static void App_Diagnostics_Capture(uint32_t now)
{
    uint32_t service;

    CAN_App_GetRxStats(&diagnostics_snapshot.can_rx);
    CAN_Transport_GetStats(&diagnostics_snapshot.can_tx);
    App_Timing_GetSnapshot(&diagnostics_snapshot.timing);

    diagnostics_snapshot.uptime_ms = now;
    diagnostics_snapshot.update_count++;

    diagnostics_snapshot.rejected_frames_total =
        diagnostics_snapshot.can_rx.rejected_wrong_id
        + diagnostics_snapshot.can_rx.rejected_frame_format
        + diagnostics_snapshot.can_rx.rejected_dlc
        + diagnostics_snapshot.can_rx.rejected_unknown_command
        + diagnostics_snapshot.can_rx.rejected_invalid_payload
        + diagnostics_snapshot.can_rx.rejected_access_denied;

    if (diagnostics_snapshot.can_rx.hal_rx_errors != 0U)
    {
        diagnostics_snapshot.latched_issue_flags |=
            APP_DIAGNOSTICS_ISSUE_CAN_RX_HAL;
    }

    if (diagnostics_snapshot.can_tx.hal_error != 0U)
    {
        diagnostics_snapshot.latched_issue_flags |=
            APP_DIAGNOSTICS_ISSUE_CAN_TX_HAL;
    }

    if (diagnostics_snapshot.can_tx.queue_overflow != 0U)
    {
        diagnostics_snapshot.latched_issue_flags |=
            APP_DIAGNOSTICS_ISSUE_TX_QUEUE_OVERFLOW;
    }

    if (diagnostics_snapshot.can_tx.queue_overflow <
        last_logged_queue_overflow_count)
    {
        /* Transport was reinitialized; establish a new counter baseline. */
        last_logged_queue_overflow_count =
            diagnostics_snapshot.can_tx.queue_overflow;
    }
    else if (diagnostics_snapshot.can_tx.queue_overflow >
             last_logged_queue_overflow_count)
    {
        uint32_t overflow_delta =
            diagnostics_snapshot.can_tx.queue_overflow -
            last_logged_queue_overflow_count;

        (void)App_Log_Push(APP_LOG_SOURCE_CAN,
                           APP_LOG_SEVERITY_WARNING,
                           APP_LOG_EVENT_CAN_TX_QUEUE_OVERFLOW,
                           overflow_delta,
                           diagnostics_snapshot.can_tx.queue_overflow);

        last_logged_queue_overflow_count =
            diagnostics_snapshot.can_tx.queue_overflow;
    }

    if (diagnostics_snapshot.can_tx.queue_stuck_events != 0U)
    {
        diagnostics_snapshot.latched_issue_flags |=
            APP_DIAGNOSTICS_ISSUE_TX_QUEUE_STUCK;
    }

    if (diagnostics_snapshot.can_rx.rx_budget_hits != 0U)
    {
        diagnostics_snapshot.latched_issue_flags |=
            APP_DIAGNOSTICS_ISSUE_CAN_RX_BUDGET;
    }

    if ((diagnostics_snapshot.can_rx.rx_watermark_events != 0U) ||
        (diagnostics_snapshot.can_rx.rx_full_events != 0U))
    {
        diagnostics_snapshot.latched_issue_flags |=
            APP_DIAGNOSTICS_ISSUE_CAN_RX_PRESSURE;
    }

    if (diagnostics_snapshot.can_rx.rx_message_lost_events != 0U)
    {
        diagnostics_snapshot.latched_issue_flags |=
            APP_DIAGNOSTICS_ISSUE_CAN_RX_LOST;
    }

    if (diagnostics_snapshot.can_rx.rx_budget_hits <
        last_logged_rx_budget_hit_count)
    {
        last_logged_rx_budget_hit_count =
            diagnostics_snapshot.can_rx.rx_budget_hits;
    }
    else if (diagnostics_snapshot.can_rx.rx_budget_hits >
             last_logged_rx_budget_hit_count)
    {
        uint32_t hit_delta =
            diagnostics_snapshot.can_rx.rx_budget_hits -
            last_logged_rx_budget_hit_count;

        (void)App_Log_Push(
            APP_LOG_SOURCE_CAN,
            APP_LOG_SEVERITY_WARNING,
            APP_LOG_EVENT_CAN_RX_BUDGET_EXHAUSTED,
            hit_delta,
            diagnostics_snapshot.can_rx.rx_budget_hits);

        last_logged_rx_budget_hit_count =
            diagnostics_snapshot.can_rx.rx_budget_hits;
    }

    App_Diagnostics_LogCounterDelta(
        diagnostics_snapshot.can_rx.rx_watermark_events,
        &last_logged_rx_watermark_count,
        APP_LOG_SEVERITY_WARNING,
        APP_LOG_EVENT_CAN_RX_FIFO_WATERMARK);
    App_Diagnostics_LogCounterDelta(
        diagnostics_snapshot.can_rx.rx_full_events,
        &last_logged_rx_full_count,
        APP_LOG_SEVERITY_WARNING,
        APP_LOG_EVENT_CAN_RX_FIFO_FULL);
    App_Diagnostics_LogCounterDelta(
        diagnostics_snapshot.can_rx.rx_message_lost_events,
        &last_logged_rx_message_lost_count,
        APP_LOG_SEVERITY_FAULT,
        APP_LOG_EVENT_CAN_RX_MESSAGE_LOST);

    for (service = 0U;
         service < (uint32_t)APP_TIMING_SERVICE_COUNT;
         service++)
    {
        uint32_t current_count =
            diagnostics_snapshot.timing.service[service].overrun_count;
        uint32_t previous_count =
            last_logged_timing_overrun_count[service];

        if (current_count < previous_count)
        {
            last_logged_timing_overrun_count[service] = current_count;
        }
        else if (current_count > previous_count)
        {
            uint32_t delta = current_count - previous_count;
            uint32_t packed_service_delta =
                (service & 0xFFU) |
                ((delta > 0x00FFFFFFUL ? 0x00FFFFFFUL : delta) << 8U);

            diagnostics_snapshot.latched_issue_flags |=
                APP_DIAGNOSTICS_ISSUE_TIMING_OVERRUN;
            (void)App_Log_Push(
                APP_LOG_SOURCE_SYSTEM,
                APP_LOG_SEVERITY_WARNING,
                APP_LOG_EVENT_TIMING_OVERRUN,
                packed_service_delta,
                App_Timing_CyclesToMicroseconds(
                    diagnostics_snapshot.timing
                        .service[service].maximum_cycles));
            last_logged_timing_overrun_count[service] = current_count;
        }
    }
}

void App_Diagnostics_Init(void)
{
    uint32_t service;

    diagnostics_snapshot = (App_Diagnostics_Snapshot_t){0};
    last_logged_queue_overflow_count = 0U;
    last_logged_rx_budget_hit_count = 0U;
    last_logged_rx_watermark_count = 0U;
    last_logged_rx_full_count = 0U;
    last_logged_rx_message_lost_count = 0U;
    for (service = 0U;
         service < (uint32_t)APP_TIMING_SERVICE_COUNT;
         service++)
    {
        last_logged_timing_overrun_count[service] = 0U;
    }
    last_diagnostics_update_tick = HAL_GetTick();
    App_Diagnostics_Capture(last_diagnostics_update_tick);
}

void App_Diagnostics_Process(void)
{
    uint32_t now = HAL_GetTick();

    if ((now - last_diagnostics_update_tick) <
        APP_DIAGNOSTICS_UPDATE_PERIOD_MS)
    {
        return;
    }

    last_diagnostics_update_tick = now;
    App_Diagnostics_Capture(now);
}

void App_Diagnostics_GetSnapshot(App_Diagnostics_Snapshot_t *snapshot)
{
    if (snapshot != NULL)
    {
        *snapshot = diagnostics_snapshot;
    }
}
