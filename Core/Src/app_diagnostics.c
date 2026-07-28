#include "main.h"
#include "app_diagnostics.h"
#include "app_log.h"

#define APP_DIAGNOSTICS_UPDATE_PERIOD_MS 100U

static App_Diagnostics_Snapshot_t diagnostics_snapshot;
static uint32_t last_diagnostics_update_tick;
static uint32_t last_logged_queue_overflow_count;
static uint32_t last_logged_rx_budget_hit_count;

static void App_Diagnostics_Capture(uint32_t now)
{
    CAN_App_GetRxStats(&diagnostics_snapshot.can_rx);
    CAN_Transport_GetStats(&diagnostics_snapshot.can_tx);

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
}

void App_Diagnostics_Init(void)
{
    diagnostics_snapshot = (App_Diagnostics_Snapshot_t){0};
    last_logged_queue_overflow_count = 0U;
    last_logged_rx_budget_hit_count = 0U;
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
