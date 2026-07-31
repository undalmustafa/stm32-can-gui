#include "main.h"
#include "can_recovery.h"
#include "app_log.h"

#define CAN_RECOVERY_RETRY_INTERVAL_MS        200U
#define CAN_RECOVERY_TX_BUFFER_MASK           0xFFFFFFFFUL

extern FDCAN_HandleTypeDef hfdcan1;

typedef enum
{
    CAN_RECOVERY_PHASE_STOP = 0,
    CAN_RECOVERY_PHASE_START,
    CAN_RECOVERY_PHASE_NOTIFICATION
} CAN_Recovery_Phase_t;

/* Written only by the ISR and read by the main loop. */
static volatile uint32_t bus_off_event_count = 0U;
static volatile uint32_t last_bus_off_psr = 0U;
static volatile uint32_t last_bus_off_ecr = 0U;
static volatile uint32_t error_passive_event_count = 0U;
static volatile uint32_t last_error_passive_psr = 0U;
static volatile uint32_t last_error_passive_ecr = 0U;
static volatile uint32_t tx_complete_event_count = 0U;

/* Main-loop-owned recovery state. */
static uint32_t handled_bus_off_event_count = 0U;
static uint32_t scheduled_bus_off_event_count = 0U;
static uint32_t logged_bus_off_event_count = 0U;
static uint32_t logged_error_passive_event_count = 0U;
static uint32_t verification_bus_off_event_count = 0U;
static uint32_t verification_tx_complete_snapshot = 0U;
static uint32_t next_recovery_attempt_tick = 0U;
static uint8_t recovery_verification_pending = 0U;
static uint8_t bus_off_fault_episode_active = 0U;
static uint32_t bus_off_log_record_count = 0U;
static uint32_t bus_off_log_suppressed_count = 0U;
static uint32_t error_passive_log_record_count = 0U;
static uint32_t error_passive_log_suppressed_count = 0U;
static CAN_Recovery_Phase_t recovery_phase = CAN_RECOVERY_PHASE_STOP;
static CAN_Recovery_Stats_t recovery_stats;

void CAN_Recovery_Init(void)
{
    bus_off_event_count = 0U;
    last_bus_off_psr = 0U;
    last_bus_off_ecr = 0U;
    error_passive_event_count = 0U;
    last_error_passive_psr = 0U;
    last_error_passive_ecr = 0U;
    tx_complete_event_count = 0U;

    handled_bus_off_event_count = 0U;
    scheduled_bus_off_event_count = 0U;
    logged_bus_off_event_count = 0U;
    logged_error_passive_event_count = 0U;
    verification_bus_off_event_count = 0U;
    verification_tx_complete_snapshot = 0U;
    next_recovery_attempt_tick = 0U;
    recovery_verification_pending = 0U;
    bus_off_fault_episode_active = 0U;
    bus_off_log_record_count = 0U;
    bus_off_log_suppressed_count = 0U;
    error_passive_log_record_count = 0U;
    error_passive_log_suppressed_count = 0U;
    recovery_phase = CAN_RECOVERY_PHASE_STOP;
    recovery_stats = (CAN_Recovery_Stats_t){0};
    recovery_stats.last_hal_status = HAL_OK;
    recovery_stats.last_failed_step = CAN_RECOVERY_STEP_NONE;
}

static void CAN_Recovery_RecordFailure(CAN_Recovery_Step_t failed_step,
                                       HAL_StatusTypeDef hal_status)
{
    uint8_t is_new_failure =
        ((recovery_stats.last_failed_step != failed_step) ||
         (recovery_stats.last_hal_status != hal_status)) ? 1U : 0U;

    recovery_stats.recovery_failures++;
    recovery_stats.last_hal_status = hal_status;
    recovery_stats.last_failed_step = failed_step;

    if (is_new_failure != 0U)
    {
        (void)App_Log_Push(APP_LOG_SOURCE_CAN,
                           APP_LOG_SEVERITY_FAULT,
                           APP_LOG_EVENT_CAN_RECOVERY_FAILED,
                           (uint32_t)failed_step,
                           (uint32_t)hal_status);
    }
}

/*
 * ISR context: keep this callback minimal. A monotonic event counter prevents
 * a second bus-off event from being lost while the main loop is recovering
 * from the first one.
 */
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan,
                                   uint32_t ErrorStatusITs)
{
    if ((hfdcan == NULL) || (hfdcan->Instance != FDCAN1))
    {
        return;
    }

    if ((ErrorStatusITs & FDCAN_IT_ERROR_PASSIVE) != 0U)
    {
        last_error_passive_psr = hfdcan->Instance->PSR;
        last_error_passive_ecr = hfdcan->Instance->ECR;
        error_passive_event_count++;
    }

    if ((ErrorStatusITs & FDCAN_IT_BUS_OFF) != 0U)
    {
        last_bus_off_psr = hfdcan->Instance->PSR;
        last_bus_off_ecr = hfdcan->Instance->ECR;
        bus_off_event_count++;
    }
}

/*
 * A TX-complete interrupt is the confirmation that the controller actually
 * put a frame on the bus. Merely accepting a frame into the TX FIFO or
 * returning HAL_OK from HAL_FDCAN_Start() is not sufficient.
 */
void HAL_FDCAN_TxBufferCompleteCallback(FDCAN_HandleTypeDef *hfdcan,
                                        uint32_t BufferIndexes)
{
    if ((hfdcan != NULL) &&
        (hfdcan->Instance == FDCAN1) &&
        (BufferIndexes != 0U))
    {
        tx_complete_event_count++;
    }
}

HAL_StatusTypeDef CAN_Recovery_EnableNotifications(
    FDCAN_HandleTypeDef *hfdcan)
{
    /*
     * Recovery restarts the peripheral, so the complete application
     * notification contract must be restored here as one atomic policy.
     */
    return HAL_FDCAN_ActivateNotification(
        hfdcan,
        FDCAN_IT_BUS_OFF |
        FDCAN_IT_ERROR_PASSIVE |
        FDCAN_IT_TX_COMPLETE |
        FDCAN_IT_RX_FIFO0_NEW_MESSAGE |
        FDCAN_IT_RX_FIFO0_WATERMARK |
        FDCAN_IT_RX_FIFO0_FULL |
        FDCAN_IT_RX_FIFO0_MESSAGE_LOST,
        CAN_RECOVERY_TX_BUFFER_MASK);
}

/*
 * Main-loop context. Successful phases are not repeated if a later phase
 * fails. For example, after a successful Stop and failed Start, the next
 * rate-limited attempt resumes directly from Start.
 */
void CAN_Handle_BusOff_Recovery(void)
{
    uint32_t now;
    uint32_t event_snapshot;
    HAL_StatusTypeDef hal_status;

    /*
     * Error Passive is a separate protocol state and does not imply Bus-Off.
     * Record its transition in main-loop context; never write the log in ISR.
     */
    if (error_passive_event_count != logged_error_passive_event_count)
    {
        uint32_t new_event_count;

        event_snapshot = error_passive_event_count;
        new_event_count =
            event_snapshot - logged_error_passive_event_count;

        if (bus_off_fault_episode_active == 0U)
        {
            (void)App_Log_Push(APP_LOG_SOURCE_CAN,
                               APP_LOG_SEVERITY_WARNING,
                               APP_LOG_EVENT_CAN_ERROR_PASSIVE,
                               last_error_passive_psr,
                               last_error_passive_ecr);
            error_passive_log_record_count++;

            if (new_event_count > 1U)
            {
                error_passive_log_suppressed_count += new_event_count - 1U;
            }
        }
        else
        {
            error_passive_log_suppressed_count += new_event_count;
        }

        logged_error_passive_event_count = event_snapshot;
    }

    now = HAL_GetTick();

    /*
     * A newly observed Bus-Off invalidates an outstanding verification.
     * Recovery retries stay at a fixed interval for deterministic return-to-
     * service latency. Repeated records from one uninterrupted physical fault
     * episode are coalesced instead of delaying the recovery attempt.
     */
    if (bus_off_event_count != scheduled_bus_off_event_count)
    {
        event_snapshot = bus_off_event_count;

        if (logged_bus_off_event_count != event_snapshot)
        {
            uint32_t new_event_count =
                event_snapshot - logged_bus_off_event_count;

            if (bus_off_fault_episode_active == 0U)
            {
                (void)App_Log_Push(APP_LOG_SOURCE_CAN,
                                   APP_LOG_SEVERITY_FAULT,
                                   APP_LOG_EVENT_CAN_BUS_OFF,
                                   last_bus_off_psr,
                                   last_bus_off_ecr);
                bus_off_log_record_count++;
                bus_off_fault_episode_active = 1U;

                if (new_event_count > 1U)
                {
                    bus_off_log_suppressed_count += new_event_count - 1U;
                }
            }
            else
            {
                bus_off_log_suppressed_count += new_event_count;
            }

            logged_bus_off_event_count = event_snapshot;
        }

        if (recovery_verification_pending != 0U)
        {
            recovery_verification_pending = 0U;
            recovery_stats.verification_failures++;
        }

        scheduled_bus_off_event_count = event_snapshot;
        next_recovery_attempt_tick =
            now + CAN_RECOVERY_RETRY_INTERVAL_MS;
    }

    /* Confirm recovery only after an actual frame transmission completes. */
    if (recovery_verification_pending != 0U)
    {
        if ((tx_complete_event_count !=
             verification_tx_complete_snapshot) &&
            (bus_off_event_count ==
             verification_bus_off_event_count))
        {
            handled_bus_off_event_count =
                verification_bus_off_event_count;
            recovery_verification_pending = 0U;
            bus_off_fault_episode_active = 0U;
            recovery_stats.recovery_successes++;
            recovery_stats.last_hal_status = HAL_OK;
            recovery_stats.last_failed_step = CAN_RECOVERY_STEP_NONE;

            (void)App_Log_Push(APP_LOG_SOURCE_CAN,
                               APP_LOG_SEVERITY_INFO,
                               APP_LOG_EVENT_CAN_RECOVERY_OK,
                               hfdcan1.Instance->PSR,
                               hfdcan1.Instance->ECR);
        }

        return;
    }

    if (scheduled_bus_off_event_count == handled_bus_off_event_count)
    {
        return;
    }

    /* Signed subtraction keeps the comparison valid across tick wraparound. */
    if ((int32_t)(now - next_recovery_attempt_tick) < 0)
    {
        return;
    }

    next_recovery_attempt_tick = now + CAN_RECOVERY_RETRY_INTERVAL_MS;
    recovery_stats.recovery_attempts++;

    if (recovery_phase == CAN_RECOVERY_PHASE_STOP)
    {
        hal_status = HAL_FDCAN_Stop(&hfdcan1);
        if (hal_status != HAL_OK)
        {
            CAN_Recovery_RecordFailure(CAN_RECOVERY_STEP_STOP, hal_status);
            return;
        }

        recovery_phase = CAN_RECOVERY_PHASE_START;
    }

    if (recovery_phase == CAN_RECOVERY_PHASE_START)
    {
        hal_status = HAL_FDCAN_Start(&hfdcan1);
        if (hal_status != HAL_OK)
        {
            CAN_Recovery_RecordFailure(CAN_RECOVERY_STEP_START, hal_status);
            return;
        }

        recovery_phase = CAN_RECOVERY_PHASE_NOTIFICATION;
    }

    hal_status = CAN_Recovery_EnableNotifications(&hfdcan1);

    if (hal_status != HAL_OK)
    {
        CAN_Recovery_RecordFailure(CAN_RECOVERY_STEP_NOTIFICATION, hal_status);
        return;
    }

    recovery_phase = CAN_RECOVERY_PHASE_STOP;
    recovery_stats.last_hal_status = HAL_OK;
    recovery_stats.last_failed_step = CAN_RECOVERY_STEP_NONE;
    verification_bus_off_event_count = scheduled_bus_off_event_count;
    verification_tx_complete_snapshot = tx_complete_event_count;
    recovery_verification_pending = 1U;
}

void CAN_Recovery_GetStats(CAN_Recovery_Stats_t *stats)
{
    if (stats != NULL)
    {
        *stats = recovery_stats;
        stats->bus_off_events = bus_off_event_count;
        stats->tx_complete_events = tx_complete_event_count;
        stats->retry_interval_ms = CAN_RECOVERY_RETRY_INTERVAL_MS;
        stats->bus_off_log_records = bus_off_log_record_count;
        stats->bus_off_log_suppressed = bus_off_log_suppressed_count;
        stats->error_passive_log_records =
            error_passive_log_record_count;
        stats->error_passive_log_suppressed =
            error_passive_log_suppressed_count;
        stats->recovery_pending =
            ((scheduled_bus_off_event_count !=
              handled_bus_off_event_count) ||
             (recovery_verification_pending != 0U)) ? 1U : 0U;
        stats->verification_pending = recovery_verification_pending;
    }
}
