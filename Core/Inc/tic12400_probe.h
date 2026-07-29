#ifndef TIC12400_PROBE_H
#define TIC12400_PROBE_H

#include "tic12400.h"
#include "tic12400_profile.h"

#include <stdint.h>

typedef struct
{
    uint32_t attempts;
    uint32_t interrupt_count;
    uint32_t first_rx_frame;
    uint32_t clear_rx_frame;
    uint32_t tx_frame;
    uint32_t rx_frame;
    uint32_t register_data;
    uint32_t int_status;
    uint32_t hal_error;
    uint32_t validation_target;
    uint32_t validation_completed;
    uint32_t validation_first_failure_index;
    uint32_t validation_first_failure_rx_frame;
    uint32_t configuration_write_count;
    uint32_t configuration_failure_address;
    uint32_t configuration_expected;
    uint32_t configuration_actual;
    uint32_t crc_poll_target;
    uint32_t crc_poll_completed;
    uint32_t crc_int_status;
    uint32_t crc_value;
    uint32_t config_readback;
    uint32_t in_en_readback;
    uint32_t cs_select_readback;
    uint32_t wc_cfg0_readback;
    uint32_t wc_cfg1_readback;
    uint32_t thres_comp_readback;
    uint32_t int_en_comp1_readback;
    uint32_t int_en_comp2_readback;
    uint32_t int_en_cfg0_readback;
    uint32_t mode_readback;
    uint32_t enabled_input_mask;
    uint32_t adc_valid_mask;
    uint32_t comparator_valid_mask;
    uint32_t battery_capable_mask;
    uint32_t battery_switch_mask;
    uint32_t comparator_readback;
    uint32_t last_int_status;
    uint32_t last_nonzero_int_status;
    uint32_t service_attempts;
    uint32_t service_failures;
    uint32_t first_service_failure_attempt;
    uint32_t first_service_failure_tx_frame;
    uint32_t first_service_failure_rx_frame;
    uint32_t first_service_failure_hal_error;
    uint32_t last_service_failure_attempt;
    uint32_t last_service_failure_tx_frame;
    uint32_t last_service_failure_rx_frame;
    uint32_t last_service_failure_hal_error;
    uint32_t consecutive_service_failures;
    uint32_t offline_events;
    uint32_t reinitialization_attempts;
    uint32_t reinitialization_successes;
    uint32_t reinitialization_delay_ms;
    uint32_t comparator_sample_batches;
    uint32_t ssc_events;
    uint32_t closed_switch_bitmap;
    uint32_t switch_valid_mask;
    uint32_t last_switch_change_mask;
    uint32_t switch_change_events;
    TIC12400_Result_t first_result;
    TIC12400_Result_t clear_result;
    TIC12400_Result_t validation_first_failure_result;
    TIC12400_Result_t configuration_result;
    TIC12400_Result_t crc_result;
    TIC12400_Result_t service_result;
    TIC12400_Result_t first_service_failure_result;
    TIC12400_Result_t last_service_failure_result;
    TIC12400_ProfileResult_t profile_result;
    TIC12400_Result_t result;
    HAL_StatusTypeDef hal_status;
    TIC12400_StatusFlags_t status;
    uint8_t device_id;
    uint8_t online;
    uint8_t por_observed;
    uint8_t interrupt_pending;
    uint8_t recovery_attempted;
    uint8_t recovery_succeeded;
    uint8_t reinitialization_pending;
    uint8_t validation_passed;
    uint8_t configuration_completed;
    uint8_t configuration_passed;
    uint8_t crc_trigger_self_cleared;
    uint8_t crc_completed;
    uint8_t adc_characterization_active;
    uint8_t monitoring_started;
    uint8_t baseline_established;
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

void TIC12400_Probe_Init(SPI_HandleTypeDef *spi);
void TIC12400_Probe_Process(void);
void TIC12400_Probe_NotifyInterruptFromIsr(void);
uint8_t TIC12400_Probe_SetBatterySwitchMask(uint32_t battery_switch_mask);
uint8_t TIC12400_Probe_GetSwitchState(
    TIC12400_ProbeSwitchState_t *state);

#endif /* TIC12400_PROBE_H */
