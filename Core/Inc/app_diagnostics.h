#ifndef APP_DIAGNOSTICS_H
#define APP_DIAGNOSTICS_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: none
 */

#include <stdint.h>

#include "can_app.h"
#include "can_transport.h"
#include "app_timing.h"

typedef enum
{
    APP_DIAGNOSTICS_ISSUE_NONE = 0U,
    APP_DIAGNOSTICS_ISSUE_CAN_RX_HAL = (1UL << 0),
    APP_DIAGNOSTICS_ISSUE_CAN_TX_HAL = (1UL << 1),
    APP_DIAGNOSTICS_ISSUE_TX_QUEUE_OVERFLOW = (1UL << 2),
    APP_DIAGNOSTICS_ISSUE_TX_QUEUE_STUCK = (1UL << 3),
    APP_DIAGNOSTICS_ISSUE_CAN_RX_BUDGET = (1UL << 4),
    APP_DIAGNOSTICS_ISSUE_CAN_RX_PRESSURE = (1UL << 5),
    APP_DIAGNOSTICS_ISSUE_CAN_RX_LOST = (1UL << 6),
    APP_DIAGNOSTICS_ISSUE_TIMING_OVERRUN = (1UL << 7)
} App_Diagnostics_IssueFlag_t;

typedef struct
{
    uint32_t uptime_ms;
    uint32_t update_count;
    uint32_t rejected_frames_total;
    uint32_t latched_issue_flags;
    CAN_App_RxStats_t can_rx;
    CAN_Transport_Stats_t can_tx;
    App_Timing_Snapshot_t timing;
} App_Diagnostics_Snapshot_t;

void App_Diagnostics_Init(void);
void App_Diagnostics_Process(void);
void App_Diagnostics_GetSnapshot(App_Diagnostics_Snapshot_t *snapshot);

#endif /* APP_DIAGNOSTICS_H */
