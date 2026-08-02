#include "tic12400_probe.h"

#include "main.h"
#include "tic12400_profile.h"
#include "tic12400_recovery.h"
#include "tic12400_switch.h"

#define TIC12400_PROBE_FALLBACK_POLL_MS  500U
#define TIC12400_PROBE_CRC_POLL_READS    100U

#define TIC12400_CONFIG_TRIGGER                (1UL << 11)
#define TIC12400_CONFIG_DET_FILTER_3_SAMPLES   (2UL << 14)
#define TIC12400_CONFIG_BINARY_BASE            \
    TIC12400_CONFIG_DET_FILTER_3_SAMPLES
#define TIC12400_ALL_WC_CFG0_1_MA              0x249249UL
#define TIC12400_ALL_WC_CFG1_1_MA              0x049249UL
#define TIC12400_COMPARATOR_THRESHOLD_2V       0U
#define TIC12400_INT_ENABLE_SSC                (1UL << 2)
#define TIC12400_INT_STATUS_SSC                (1UL << 3)

volatile TIC12400_ProbeSnapshot_t g_tic12400_probe;

typedef struct
{
    uint32_t service_failures;
    uint32_t last_nonzero_int_status;
} TIC12400_ProbeLifetimeDiagnostics_t;

static TIC12400_Device_t tic12400_device;
static TIC12400_Profile_t tic12400_active_profile;
static TIC12400_RecoveryState_t tic12400_recovery_state;
static TIC12400_SwitchFilter_t tic12400_switch_filter;
static uint32_t tic12400_last_service_tick;
static uint32_t tic12400_requested_battery_switch_mask;
static uint32_t tic12400_applied_battery_switch_mask;
static uint8_t tic12400_profile_generation;
static uint8_t tic12400_profile_applied;
static uint8_t tic12400_peripheral_ready;

static void TIC12400_ProbeResetAttemptSnapshot(void)
{
    TIC12400_ProbeLifetimeDiagnostics_t previous;

    HAL_NVIC_DisableIRQ(TIC12400_INT_EXTI_IRQn);
    previous.service_failures =
        g_tic12400_probe.service_failures;
    previous.last_nonzero_int_status =
        g_tic12400_probe.last_nonzero_int_status;

    g_tic12400_probe = (TIC12400_ProbeSnapshot_t){0};
    g_tic12400_probe.service_failures = previous.service_failures;
    g_tic12400_probe.last_nonzero_int_status =
        previous.last_nonzero_int_status;
    HAL_NVIC_EnableIRQ(TIC12400_INT_EXTI_IRQn);
}

static void TIC12400_ProbeMarkOffline(void)
{
    g_tic12400_probe.online = 0U;
    g_tic12400_probe.monitoring_started = 0U;
    g_tic12400_probe.switch_state_valid = 0U;
    g_tic12400_probe.switch_valid_mask = 0U;
    g_tic12400_probe.interrupt_pending = 0U;
}

static void TIC12400_ProbeStoreTransaction(
    const TIC12400_Transaction_t *transaction)
{
    g_tic12400_probe.result = transaction->result;
    g_tic12400_probe.status = transaction->status;
}

static uint8_t TIC12400_ProbeCanClearLatchedFault(
    const TIC12400_Transaction_t *transaction)
{
    return ((transaction->result == TIC12400_RESULT_DEVICE_SPI_ERROR) ||
            (transaction->result ==
             TIC12400_RESULT_DEVICE_PARITY_ERROR)) ? 1U : 0U;
}

static uint8_t TIC12400_ProbeWriteAndVerify(
    uint8_t register_address,
    uint32_t expected_value)
{
    TIC12400_Transaction_t transaction;

    transaction = TIC12400_WriteRegister(
        &tic12400_device,
        register_address,
        expected_value);
    TIC12400_ProbeStoreTransaction(&transaction);
    if (transaction.result != TIC12400_RESULT_OK)
    {
        g_tic12400_probe.configuration_result = transaction.result;
        return 0U;
    }

    transaction = TIC12400_ReadRegister(
        &tic12400_device,
        register_address);
    TIC12400_ProbeStoreTransaction(&transaction);
    if ((transaction.result != TIC12400_RESULT_OK) ||
        (transaction.data != expected_value))
    {
        g_tic12400_probe.configuration_result =
            (transaction.result != TIC12400_RESULT_OK) ?
            transaction.result :
            TIC12400_RESULT_REGISTER_VERIFY_MISMATCH;
        return 0U;
    }

    return 1U;
}

static uint8_t TIC12400_ProbeRunConfigurationCrc(
    uint32_t config_value)
{
    uint32_t config_readback = 0U;
    uint32_t index;
    TIC12400_Transaction_t transaction;
    uint8_t trigger_cleared = 0U;

    transaction = TIC12400_WriteRegister(
        &tic12400_device,
        TIC12400_REGISTER_CONFIG,
        config_value | TIC12400_CONFIG_CRC_TRIGGER_MASK);
    TIC12400_ProbeStoreTransaction(&transaction);
    if (transaction.result != TIC12400_RESULT_OK)
    {
        g_tic12400_probe.configuration_result = transaction.result;
        return 0U;
    }

    /*
     * CRC_T is self-clearing. Do not write any configuration register while
     * it remains set; doing so would make the device's CRC result invalid.
     */
    for (index = 0U;
         index < TIC12400_PROBE_CRC_POLL_READS;
         index++)
    {
        transaction = TIC12400_ReadRegister(
            &tic12400_device,
            TIC12400_REGISTER_CONFIG);
        TIC12400_ProbeStoreTransaction(&transaction);
        config_readback = transaction.data;

        if (transaction.result != TIC12400_RESULT_OK)
        {
            g_tic12400_probe.configuration_result =
                transaction.result;
            return 0U;
        }

        if ((transaction.data &
             TIC12400_CONFIG_CRC_TRIGGER_MASK) == 0U)
        {
            trigger_cleared = 1U;
            break;
        }
    }

    if (trigger_cleared == 0U)
    {
        g_tic12400_probe.configuration_result =
            TIC12400_RESULT_CRC_TIMEOUT;
        return 0U;
    }

    if (config_readback != config_value)
    {
        g_tic12400_probe.configuration_result =
            TIC12400_RESULT_REGISTER_VERIFY_MISMATCH;
        return 0U;
    }

    transaction = TIC12400_ReadRegister(
        &tic12400_device,
        TIC12400_REGISTER_INT_STAT);
    TIC12400_ProbeStoreTransaction(&transaction);
    if (transaction.result != TIC12400_RESULT_OK)
    {
        g_tic12400_probe.configuration_result = transaction.result;
        return 0U;
    }

    if ((transaction.data &
         TIC12400_INT_STATUS_CRC_CALC_MASK) == 0U)
    {
        g_tic12400_probe.configuration_result =
            TIC12400_RESULT_CRC_COMPLETION_MISSING;
        return 0U;
    }

    transaction =
        TIC12400_ReadConfigurationCrc(&tic12400_device);
    TIC12400_ProbeStoreTransaction(&transaction);
    if (transaction.result != TIC12400_RESULT_OK)
    {
        g_tic12400_probe.configuration_result = transaction.result;
        return 0U;
    }

    g_tic12400_probe.crc_completed = 1U;
    return 1U;
}

static void TIC12400_ProbeRecordServiceFailure(
    const TIC12400_Transaction_t *transaction)
{
    g_tic12400_probe.service_failures++;
    g_tic12400_probe.service_result = transaction->result;
}

static uint8_t TIC12400_ProbeSampleInputs(void)
{
    uint32_t closed_mask;
    TIC12400_Transaction_t interrupt_status;
    TIC12400_Transaction_t comparator_status;

    /*
     * Clear the event which caused the present service. An interrupt arriving
     * during the following SPI reads sets the flag again in the EXTI callback.
     */
    g_tic12400_probe.interrupt_pending = 0U;

    interrupt_status = TIC12400_ReadRegister(
        &tic12400_device,
        TIC12400_REGISTER_INT_STAT);
    TIC12400_ProbeStoreTransaction(&interrupt_status);
    if (interrupt_status.result != TIC12400_RESULT_OK)
    {
        TIC12400_ProbeRecordServiceFailure(&interrupt_status);
        return 0U;
    }

    if (interrupt_status.data != 0U)
    {
        g_tic12400_probe.last_nonzero_int_status =
            interrupt_status.data;
    }
    /*
     * INT_STAT retains SSC until it is read, so the 500 ms fallback service
     * also recovers a missed EXTI edge. Other interrupts do not require a
     * switch-state register read.
     */
    if ((interrupt_status.data & TIC12400_INT_STATUS_SSC) == 0U)
    {
        g_tic12400_probe.service_result = TIC12400_RESULT_OK;
        return 1U;
    }

    comparator_status = TIC12400_ReadRegister(
        &tic12400_device,
        TIC12400_REGISTER_IN_STAT_COMP);
    TIC12400_ProbeStoreTransaction(&comparator_status);
    if (comparator_status.result != TIC12400_RESULT_OK)
    {
        TIC12400_ProbeRecordServiceFailure(&comparator_status);
        return 0U;
    }

    closed_mask = TIC12400_Profile_DecodeComparatorClosed(
        &tic12400_active_profile,
        comparator_status.data & TIC12400_PROFILE_CHANNEL_MASK);
    (void)TIC12400_SwitchFilter_CommitDebouncedMask(
        &tic12400_switch_filter,
        closed_mask,
        tic12400_active_profile.enabled_mask);
    g_tic12400_probe.closed_switch_bitmap =
        tic12400_switch_filter.stable_closed_mask;
    g_tic12400_probe.switch_valid_mask =
        tic12400_switch_filter.valid_mask;
    g_tic12400_probe.switch_state_generation =
        tic12400_switch_filter.generation;
    g_tic12400_probe.switch_state_valid =
        (tic12400_switch_filter.valid_mask ==
         tic12400_active_profile.enabled_mask) ? 1U : 0U;
    if (tic12400_switch_filter.last_change_mask != 0U)
    {
        g_tic12400_probe.last_switch_change_mask =
            tic12400_switch_filter.last_change_mask;
    }
    g_tic12400_probe.service_result = TIC12400_RESULT_OK;
    return 1U;
}

static void TIC12400_ProbeConfigureInputs(void)
{
    uint32_t int_en_comp1;
    uint32_t int_en_comp2;
    TIC12400_ProfileResult_t profile_result;

    g_tic12400_probe.online = 0U;
    g_tic12400_probe.configuration_passed = 0U;
    g_tic12400_probe.crc_completed = 0U;
    g_tic12400_probe.monitoring_started = 0U;
    g_tic12400_probe.switch_state_valid = 0U;
    g_tic12400_probe.switch_valid_mask = 0U;
    g_tic12400_probe.configuration_result = TIC12400_RESULT_OK;
    tic12400_active_profile =
        TIC12400_Profile_CarrierBinary();
    tic12400_active_profile.battery_switch_mask =
        tic12400_requested_battery_switch_mask;
    profile_result =
        TIC12400_Profile_Validate(
            &tic12400_active_profile,
            TIC12400_PROFILE_CARRIER_FITTED_MASK);
    if (profile_result != TIC12400_PROFILE_OK)
    {
        g_tic12400_probe.configuration_result =
            TIC12400_RESULT_INVALID_DATA;
        return;
    }

    g_tic12400_probe.battery_capable_mask =
        TIC12400_PROFILE_BATTERY_CAPABLE_MASK;
    g_tic12400_probe.battery_switch_mask =
        tic12400_active_profile.battery_switch_mask;
    int_en_comp1 =
        TIC12400_Profile_BuildComparatorInterruptEnable(
            &tic12400_active_profile,
            0U);
    int_en_comp2 =
        TIC12400_Profile_BuildComparatorInterruptEnable(
            &tic12400_active_profile,
            12U);

    /*
     * The carrier uses binary comparator inputs and hardware filtering.
     * IN0-IN9 may use battery polarity, while IN12 stays disabled.
     */
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_CONFIG,
            TIC12400_CONFIG_BINARY_BASE) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_MODE,
            0U) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_CS_SELECT,
            tic12400_active_profile.battery_switch_mask) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_WC_CFG0,
            TIC12400_ALL_WC_CFG0_1_MA) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_WC_CFG1,
            TIC12400_ALL_WC_CFG1_1_MA) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_IN_EN,
            tic12400_active_profile.enabled_mask) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_THRES_COMP,
            TIC12400_COMPARATOR_THRESHOLD_2V) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_INT_EN_COMP1,
            int_en_comp1) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_INT_EN_COMP2,
            int_en_comp2) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_INT_EN_CFG0,
            TIC12400_INT_ENABLE_SSC) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeRunConfigurationCrc(
            TIC12400_CONFIG_BINARY_BASE) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_CONFIG,
            TIC12400_CONFIG_BINARY_BASE |
            TIC12400_CONFIG_TRIGGER) == 0U)
    {
        return;
    }

    g_tic12400_probe.configuration_passed = 1U;
    g_tic12400_probe.monitoring_started = 1U;

    if ((tic12400_profile_applied == 0U) ||
        (tic12400_applied_battery_switch_mask !=
         tic12400_active_profile.battery_switch_mask))
    {
        tic12400_profile_generation++;
        tic12400_applied_battery_switch_mask =
            tic12400_active_profile.battery_switch_mask;
        tic12400_profile_applied = 1U;
    }
    g_tic12400_probe.profile_generation =
        tic12400_profile_generation;

    /*
     * tSTARTUP is at most 400 us. Allow the first detection cycle to finish,
     * then preserve and acknowledge its expected baseline SSC interrupt.
     */
    HAL_Delay(2U);
    (void)TIC12400_ProbeSampleInputs();
    tic12400_last_service_tick = HAL_GetTick();
}

static uint8_t TIC12400_ProbeAttemptInitialize(void)
{
    TIC12400_Result_t reset_result;
    TIC12400_Transaction_t transaction;
    TIC12400_Transaction_t interrupt_status;
    uint8_t recovery_attempted = 0U;

    TIC12400_ProbeResetAttemptSnapshot();
    TIC12400_SwitchFilter_Init(&tic12400_switch_filter);

    reset_result = TIC12400_HardwareReset(&tic12400_device);
    if (reset_result != TIC12400_RESULT_OK)
    {
        g_tic12400_probe.result = reset_result;
        return 0U;
    }

    transaction = TIC12400_ReadDeviceId(&tic12400_device);
    TIC12400_ProbeStoreTransaction(&transaction);
    g_tic12400_probe.device_id = (uint8_t)transaction.data;
    g_tic12400_probe.por_observed =
        transaction.status.power_on_reset;

    /*
     * SPI_FAIL and PRTY_FAIL describe the previous SI word: the status
     * flags are latched when CS falls, before the current word is shifted.
     * A valid DEVICE_ID response can therefore carry a stale fault. Read
     * the clear-on-read INT_STAT register once and retry the identity read.
     */
    if (TIC12400_ProbeCanClearLatchedFault(&transaction) != 0U)
    {
        recovery_attempted = 1U;
        interrupt_status = TIC12400_ReadRegister(
            &tic12400_device,
            TIC12400_REGISTER_INT_STAT);
        if (((interrupt_status.data & 1U) != 0U) ||
            (interrupt_status.status.power_on_reset != 0U))
        {
            g_tic12400_probe.por_observed = 1U;
        }

        /*
         * A valid INT_STAT read clears the register on the rising CS edge.
         * Its response may still report the fault being cleared.
         */
        if ((interrupt_status.hal_status == HAL_OK) &&
            (TIC12400_FrameHasOddParity(
                 interrupt_status.rx_frame) != 0U))
        {
            g_tic12400_probe.interrupt_pending = 0U;
        }

        transaction = TIC12400_ReadDeviceId(&tic12400_device);
        TIC12400_ProbeStoreTransaction(&transaction);
        g_tic12400_probe.device_id = (uint8_t)transaction.data;
        if (transaction.status.power_on_reset != 0U)
        {
            g_tic12400_probe.por_observed = 1U;
        }
    }

    if (transaction.result != TIC12400_RESULT_OK)
    {
        return 0U;
    }

    /*
     * INT_STAT is clear-on-read at the rising edge of CS. Capture POR before
     * clearing the device.
     */
    if (recovery_attempted == 0U)
    {
        interrupt_status = TIC12400_ReadRegister(
            &tic12400_device,
            TIC12400_REGISTER_INT_STAT);
        if ((interrupt_status.data & 1U) != 0U)
        {
            g_tic12400_probe.por_observed = 1U;
        }
        if (interrupt_status.result != TIC12400_RESULT_OK)
        {
            TIC12400_ProbeStoreTransaction(&interrupt_status);
            return 0U;
        }
        g_tic12400_probe.interrupt_pending = 0U;
    }

    TIC12400_ProbeConfigureInputs();
    if (g_tic12400_probe.configuration_passed != 0U)
    {
        g_tic12400_probe.online = 1U;
        return 1U;
    }
    g_tic12400_probe.result =
        g_tic12400_probe.configuration_result;

    return 0U;
}

void TIC12400_Probe_Init(SPI_HandleTypeDef *spi,
                         uint8_t peripheral_ready)
{
    uint32_t now = HAL_GetTick();
    uint8_t succeeded;

    g_tic12400_probe = (TIC12400_ProbeSnapshot_t){0};
    tic12400_device = (TIC12400_Device_t){0};
    tic12400_active_profile = (TIC12400_Profile_t){0};
    TIC12400_SwitchFilter_Init(&tic12400_switch_filter);
    tic12400_last_service_tick = now;
    tic12400_requested_battery_switch_mask = 0U;
    tic12400_applied_battery_switch_mask = 0U;
    tic12400_profile_generation = 0U;
    tic12400_profile_applied = 0U;
    tic12400_peripheral_ready =
        ((peripheral_ready != 0U) && (spi != NULL)) ? 1U : 0U;
    TIC12400_Recovery_Init(&tic12400_recovery_state);

    if (tic12400_peripheral_ready == 0U)
    {
        TIC12400_Recovery_RecordInitialResult(
            &tic12400_recovery_state,
            now,
            0U);
        TIC12400_ProbeMarkOffline();
        return;
    }

    TIC12400_Driver_Init(&tic12400_device,
                         spi,
                         TIC12400_CS_GPIO_Port,
                         TIC12400_CS_Pin,
                         TIC12400_RESET_GPIO_Port,
                         TIC12400_RESET_Pin);
    succeeded = TIC12400_ProbeAttemptInitialize();
    TIC12400_Recovery_RecordInitialResult(
        &tic12400_recovery_state,
        HAL_GetTick(),
        succeeded);
    if (succeeded == 0U)
    {
        TIC12400_ProbeMarkOffline();
    }
}

void TIC12400_Probe_Process(void)
{
    uint32_t now;
    uint8_t succeeded;
    uint8_t became_offline;

    if (tic12400_peripheral_ready == 0U)
    {
        return;
    }

    if (g_tic12400_probe.monitoring_started == 0U)
    {
        now = HAL_GetTick();
        if (TIC12400_Recovery_ShouldReinitialize(
                &tic12400_recovery_state,
                now) == 0U)
        {
            return;
        }

        succeeded = TIC12400_ProbeAttemptInitialize();
        now = HAL_GetTick();
        TIC12400_Recovery_RecordReinitializationResult(
            &tic12400_recovery_state,
            now,
            succeeded);
        if (succeeded == 0U)
        {
            TIC12400_ProbeMarkOffline();
        }
        return;
    }

    now = HAL_GetTick();
    if ((g_tic12400_probe.interrupt_pending == 0U) &&
        ((now - tic12400_last_service_tick) <
         TIC12400_PROBE_FALLBACK_POLL_MS))
    {
        return;
    }

    succeeded = TIC12400_ProbeSampleInputs();
    became_offline = TIC12400_Recovery_RecordServiceResult(
        &tic12400_recovery_state,
        now,
        succeeded);
    if (became_offline != 0U)
    {
        TIC12400_ProbeMarkOffline();
    }
    tic12400_last_service_tick = now;
}

void TIC12400_Probe_NotifyInterruptFromIsr(void)
{
    g_tic12400_probe.interrupt_pending = 1U;
}

uint8_t TIC12400_Probe_SetBatterySwitchMask(
    uint32_t battery_switch_mask)
{
    TIC12400_Profile_t requested_profile =
        TIC12400_Profile_CarrierBinary();
    uint8_t succeeded;

    if (tic12400_peripheral_ready == 0U)
    {
        return 0U;
    }

    requested_profile.battery_switch_mask = battery_switch_mask;
    if (TIC12400_Profile_Validate(
            &requested_profile,
            TIC12400_PROFILE_CARRIER_FITTED_MASK) !=
        TIC12400_PROFILE_OK)
    {
        return 0U;
    }

    if ((battery_switch_mask ==
         tic12400_requested_battery_switch_mask) &&
        (g_tic12400_probe.configuration_passed != 0U) &&
        (g_tic12400_probe.monitoring_started != 0U))
    {
        return 1U;
    }

    tic12400_requested_battery_switch_mask = battery_switch_mask;
    TIC12400_SwitchFilter_Init(&tic12400_switch_filter);
    HAL_NVIC_DisableIRQ(TIC12400_INT_EXTI_IRQn);
    TIC12400_ProbeConfigureInputs();
    HAL_NVIC_EnableIRQ(TIC12400_INT_EXTI_IRQn);

    succeeded =
        (g_tic12400_probe.configuration_passed != 0U) ? 1U : 0U;
    TIC12400_Recovery_RecordInitialResult(
        &tic12400_recovery_state,
        HAL_GetTick(),
        succeeded);
    if (succeeded != 0U)
    {
        g_tic12400_probe.online = 1U;
        g_tic12400_probe.result = TIC12400_RESULT_OK;
    }
    else
    {
        g_tic12400_probe.result =
            g_tic12400_probe.configuration_result;
        TIC12400_ProbeMarkOffline();
    }
    return succeeded;
}

uint8_t TIC12400_Probe_GetSwitchState(
    TIC12400_ProbeSwitchState_t *state)
{
    if (state == NULL)
    {
        return 0U;
    }

    state->closed_bitmap =
        g_tic12400_probe.closed_switch_bitmap;
    state->valid_mask =
        g_tic12400_probe.switch_valid_mask;
    state->last_change_mask =
        g_tic12400_probe.last_switch_change_mask;
    state->generation =
        g_tic12400_probe.switch_state_generation;
    state->data_valid =
        ((g_tic12400_probe.switch_state_valid != 0U) &&
         (g_tic12400_probe.monitoring_started != 0U) &&
         (g_tic12400_probe.service_result ==
          TIC12400_RESULT_OK)) ? 1U : 0U;
    return state->data_valid;
}

void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin)
{
    if (gpio_pin == TIC12400_INT_Pin)
    {
        TIC12400_Probe_NotifyInterruptFromIsr();
    }
}
