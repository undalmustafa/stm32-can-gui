#ifndef CAN_RECOVERY_H
#define CAN_RECOVERY_H

#include "stm32h7xx_hal.h"

/*
 * Context contract: public recovery functions run in the main loop. The HAL
 * callbacks implemented by can_recovery.c only snapshot fixed-size status and
 * increment monotonic counters; they never recover or log in ISR context.
 */

typedef enum
{
    CAN_RECOVERY_STEP_NONE = 0,
    CAN_RECOVERY_STEP_STOP,
    CAN_RECOVERY_STEP_START,
    CAN_RECOVERY_STEP_NOTIFICATION
} CAN_Recovery_Step_t;

typedef struct
{
    uint32_t bus_off_events;
    uint32_t recovery_attempts;
    uint32_t recovery_successes;
    uint32_t recovery_failures;
    uint32_t verification_failures;
    uint32_t tx_complete_events;
    uint32_t retry_interval_ms;
    uint32_t bus_off_log_records;
    uint32_t bus_off_log_suppressed;
    uint32_t error_passive_log_records;
    uint32_t error_passive_log_suppressed;
    HAL_StatusTypeDef last_hal_status;
    CAN_Recovery_Step_t last_failed_step;
    uint8_t recovery_pending;
    uint8_t verification_pending;
} CAN_Recovery_Stats_t;

/* Main-loop only; call before enabling any FDCAN interrupt notification. */
void CAN_Recovery_Init(void);
HAL_StatusTypeDef CAN_Recovery_EnableNotifications(
    FDCAN_HandleTypeDef *hfdcan);
void CAN_Handle_BusOff_Recovery(void);
void CAN_Recovery_GetStats(CAN_Recovery_Stats_t *stats);

#endif /* CAN_RECOVERY_H */
