#ifndef TIC12400_PROBE_H
#define TIC12400_PROBE_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: TIC12400_Probe_NotifyInterruptFromIsr
 * CALL_CONTEXT_INTERNAL: none
 */

#include "tic12400.h"
#include "tic12400_profile.h"

#include <stdint.h>

typedef struct
{
    uint32_t battery_capable_mask;
    uint32_t battery_switch_mask;
    uint32_t last_nonzero_int_status;
    uint32_t service_failures;
    uint32_t closed_switch_bitmap;
    uint32_t switch_valid_mask;
    uint32_t last_switch_change_mask;
    TIC12400_Result_t configuration_result;
    TIC12400_Result_t service_result;
    TIC12400_Result_t result;
    TIC12400_StatusFlags_t status;
    uint8_t device_id;
    uint8_t online;
    uint8_t por_observed;
    uint8_t interrupt_pending;
    uint8_t configuration_passed;
    uint8_t crc_completed;
    uint8_t monitoring_started;
    uint8_t switch_state_generation;
    uint8_t profile_generation;
    uint8_t switch_state_valid;
} TIC12400_ProbeSnapshot_t;

typedef struct
{
    uint32_t closed_bitmap;
    uint32_t valid_mask;
    uint32_t last_change_mask;
    uint8_t generation;
    uint8_t data_valid;
} TIC12400_ProbeSwitchState_t;

extern volatile TIC12400_ProbeSnapshot_t g_tic12400_probe;

void TIC12400_Probe_Init(SPI_HandleTypeDef *spi,
                         uint8_t peripheral_ready);
void TIC12400_Probe_Process(void);
/* ISR-safe: increments the edge counter and sets the main-loop pending flag. */
void TIC12400_Probe_NotifyInterruptFromIsr(void);
uint8_t TIC12400_Probe_SetBatterySwitchMask(uint32_t battery_switch_mask);
uint8_t TIC12400_Probe_GetSwitchState(
    TIC12400_ProbeSwitchState_t *state);

#endif /* TIC12400_PROBE_H */
