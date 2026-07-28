#include "tic12400_probe.h"

#include "main.h"

#define TIC12400_PROBE_VALIDATION_READS 1000U
#define TIC12400_PROBE_FALLBACK_POLL_MS  50U
#define TIC12400_PROBE_CRC_POLL_READS    100U

#define TIC12400_CONFIG_TRIGGER           (1UL << 11)
#define TIC12400_BOARD_FITTED_INPUT_MASK   0xFFEFFFUL
#define TIC12400_ALL_WC_CFG0_1_MA          0x249249UL
#define TIC12400_ALL_WC_CFG1_1_MA          0x049249UL
#define TIC12400_COMP1_BOTH_EDGES          0xFFFFFFUL
#define TIC12400_COMP2_BOTH_EDGES_NO_IN12  0xFFFFFCUL
#define TIC12400_INT_ENABLE_SSC            (1UL << 2)
#define TIC12400_INT_STATUS_SSC            (1UL << 3)

volatile TIC12400_ProbeSnapshot_t g_tic12400_probe;

static TIC12400_Device_t tic12400_device;
static uint32_t tic12400_last_service_tick;

static uint8_t TIC12400_ProbeCountBits(uint32_t value)
{
    uint8_t count = 0U;

    while (value != 0U)
    {
        count = (uint8_t)(count + (uint8_t)(value & 1U));
        value >>= 1U;
    }

    return count;
}

static void TIC12400_ProbeStoreTransaction(
    const TIC12400_Transaction_t *transaction)
{
    g_tic12400_probe.tx_frame = transaction->tx_frame;
    g_tic12400_probe.rx_frame = transaction->rx_frame;
    g_tic12400_probe.register_data = transaction->data;
    g_tic12400_probe.hal_error = transaction->hal_error;
    g_tic12400_probe.result = transaction->result;
    g_tic12400_probe.hal_status = transaction->hal_status;
    g_tic12400_probe.status = transaction->status;
}

static uint8_t TIC12400_ProbeCanClearLatchedFault(
    const TIC12400_Transaction_t *transaction)
{
    return ((transaction->result == TIC12400_RESULT_DEVICE_SPI_ERROR) ||
            (transaction->result ==
             TIC12400_RESULT_DEVICE_PARITY_ERROR)) ? 1U : 0U;
}

static void TIC12400_ProbeValidateCommunication(void)
{
    uint32_t index;
    TIC12400_Transaction_t transaction;

    g_tic12400_probe.validation_target =
        TIC12400_PROBE_VALIDATION_READS;

    for (index = 0U;
         index < TIC12400_PROBE_VALIDATION_READS;
         index++)
    {
        transaction = TIC12400_ReadDeviceId(&tic12400_device);
        TIC12400_ProbeStoreTransaction(&transaction);

        if (transaction.result != TIC12400_RESULT_OK)
        {
            g_tic12400_probe.validation_first_failure_index =
                index + 1U;
            g_tic12400_probe.validation_first_failure_rx_frame =
                transaction.rx_frame;
            g_tic12400_probe.validation_first_failure_result =
                transaction.result;
            return;
        }

        g_tic12400_probe.validation_completed = index + 1U;
    }

    g_tic12400_probe.validation_passed = 1U;
}

static uint8_t TIC12400_ProbeWriteAndVerify(
    uint8_t register_address,
    uint32_t expected_value,
    volatile uint32_t *readback)
{
    TIC12400_Transaction_t transaction;

    transaction = TIC12400_WriteRegister(
        &tic12400_device,
        register_address,
        expected_value);
    g_tic12400_probe.configuration_write_count++;
    TIC12400_ProbeStoreTransaction(&transaction);
    if (transaction.result != TIC12400_RESULT_OK)
    {
        g_tic12400_probe.configuration_failure_address =
            register_address;
        g_tic12400_probe.configuration_expected = expected_value;
        g_tic12400_probe.configuration_actual = transaction.data;
        g_tic12400_probe.configuration_result = transaction.result;
        return 0U;
    }

    transaction = TIC12400_ReadRegister(
        &tic12400_device,
        register_address);
    TIC12400_ProbeStoreTransaction(&transaction);
    *readback = transaction.data;

    if ((transaction.result != TIC12400_RESULT_OK) ||
        (transaction.data != expected_value))
    {
        g_tic12400_probe.configuration_failure_address =
            register_address;
        g_tic12400_probe.configuration_expected = expected_value;
        g_tic12400_probe.configuration_actual = transaction.data;
        g_tic12400_probe.configuration_result =
            (transaction.result != TIC12400_RESULT_OK) ?
            transaction.result :
            TIC12400_RESULT_REGISTER_VERIFY_MISMATCH;
        return 0U;
    }

    return 1U;
}

static uint8_t TIC12400_ProbeRunConfigurationCrc(void)
{
    uint32_t index;
    TIC12400_Transaction_t transaction;

    g_tic12400_probe.crc_result = TIC12400_RESULT_OK;
    g_tic12400_probe.crc_poll_target =
        TIC12400_PROBE_CRC_POLL_READS;

    transaction = TIC12400_WriteRegister(
        &tic12400_device,
        TIC12400_REGISTER_CONFIG,
        TIC12400_CONFIG_CRC_TRIGGER_MASK);
    g_tic12400_probe.configuration_write_count++;
    TIC12400_ProbeStoreTransaction(&transaction);
    if (transaction.result != TIC12400_RESULT_OK)
    {
        g_tic12400_probe.configuration_failure_address =
            TIC12400_REGISTER_CONFIG;
        g_tic12400_probe.configuration_expected =
            TIC12400_CONFIG_CRC_TRIGGER_MASK;
        g_tic12400_probe.configuration_actual = transaction.data;
        g_tic12400_probe.crc_result = transaction.result;
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
        g_tic12400_probe.crc_poll_completed = index + 1U;
        g_tic12400_probe.config_readback = transaction.data;

        if (transaction.result != TIC12400_RESULT_OK)
        {
            g_tic12400_probe.configuration_failure_address =
                TIC12400_REGISTER_CONFIG;
            g_tic12400_probe.configuration_expected = 0U;
            g_tic12400_probe.configuration_actual =
                transaction.data;
            g_tic12400_probe.crc_result = transaction.result;
            g_tic12400_probe.configuration_result =
                transaction.result;
            return 0U;
        }

        if ((transaction.data &
             TIC12400_CONFIG_CRC_TRIGGER_MASK) == 0U)
        {
            g_tic12400_probe.crc_trigger_self_cleared = 1U;
            break;
        }
    }

    if (g_tic12400_probe.crc_trigger_self_cleared == 0U)
    {
        g_tic12400_probe.configuration_failure_address =
            TIC12400_REGISTER_CONFIG;
        g_tic12400_probe.configuration_expected = 0U;
        g_tic12400_probe.configuration_actual =
            g_tic12400_probe.config_readback;
        g_tic12400_probe.crc_result =
            TIC12400_RESULT_CRC_TIMEOUT;
        g_tic12400_probe.configuration_result =
            TIC12400_RESULT_CRC_TIMEOUT;
        return 0U;
    }

    transaction = TIC12400_ReadRegister(
        &tic12400_device,
        TIC12400_REGISTER_INT_STAT);
    TIC12400_ProbeStoreTransaction(&transaction);
    g_tic12400_probe.crc_int_status = transaction.data;
    if (transaction.result != TIC12400_RESULT_OK)
    {
        g_tic12400_probe.configuration_failure_address =
            TIC12400_REGISTER_INT_STAT;
        g_tic12400_probe.configuration_expected =
            TIC12400_INT_STATUS_CRC_CALC_MASK;
        g_tic12400_probe.configuration_actual = transaction.data;
        g_tic12400_probe.crc_result = transaction.result;
        g_tic12400_probe.configuration_result = transaction.result;
        return 0U;
    }

    if ((transaction.data &
         TIC12400_INT_STATUS_CRC_CALC_MASK) == 0U)
    {
        g_tic12400_probe.configuration_failure_address =
            TIC12400_REGISTER_INT_STAT;
        g_tic12400_probe.configuration_expected =
            TIC12400_INT_STATUS_CRC_CALC_MASK;
        g_tic12400_probe.configuration_actual = transaction.data;
        g_tic12400_probe.crc_result =
            TIC12400_RESULT_CRC_COMPLETION_MISSING;
        g_tic12400_probe.configuration_result =
            TIC12400_RESULT_CRC_COMPLETION_MISSING;
        return 0U;
    }

    transaction =
        TIC12400_ReadConfigurationCrc(&tic12400_device);
    TIC12400_ProbeStoreTransaction(&transaction);
    if (transaction.result != TIC12400_RESULT_OK)
    {
        g_tic12400_probe.configuration_failure_address =
            TIC12400_REGISTER_CRC;
        g_tic12400_probe.configuration_expected = 0U;
        g_tic12400_probe.configuration_actual = transaction.data;
        g_tic12400_probe.crc_result = transaction.result;
        g_tic12400_probe.configuration_result = transaction.result;
        return 0U;
    }

    g_tic12400_probe.crc_value =
        transaction.data & TIC12400_CRC_VALUE_MASK;
    g_tic12400_probe.crc_completed = 1U;
    g_tic12400_probe.crc_result = TIC12400_RESULT_OK;
    return 1U;
}

static void TIC12400_ProbeRecordServiceFailure(
    const TIC12400_Transaction_t *transaction)
{
    g_tic12400_probe.service_failures++;
    g_tic12400_probe.service_result = transaction->result;

    if (g_tic12400_probe.first_service_failure_attempt == 0U)
    {
        g_tic12400_probe.first_service_failure_attempt =
            g_tic12400_probe.service_attempts;
        g_tic12400_probe.first_service_failure_tx_frame =
            transaction->tx_frame;
        g_tic12400_probe.first_service_failure_rx_frame =
            transaction->rx_frame;
        g_tic12400_probe.first_service_failure_hal_error =
            transaction->hal_error;
        g_tic12400_probe.first_service_failure_result =
            transaction->result;
    }

    g_tic12400_probe.last_service_failure_attempt =
        g_tic12400_probe.service_attempts;
    g_tic12400_probe.last_service_failure_tx_frame =
        transaction->tx_frame;
    g_tic12400_probe.last_service_failure_rx_frame =
        transaction->rx_frame;
    g_tic12400_probe.last_service_failure_hal_error =
        transaction->hal_error;
    g_tic12400_probe.last_service_failure_result =
        transaction->result;
}

static void TIC12400_ProbeSampleInputs(void)
{
    uint32_t changed_inputs;
    uint32_t previous_comparator_bitmap;
    TIC12400_Transaction_t interrupt_status;
    TIC12400_Transaction_t comparator_status;

    g_tic12400_probe.service_attempts++;

    /*
     * Clear the event which caused the present service. An interrupt arriving
     * during the following SPI reads sets the flag again in the EXTI callback.
     */
    g_tic12400_probe.interrupt_pending = 0U;

    interrupt_status = TIC12400_ReadRegister(
        &tic12400_device,
        TIC12400_REGISTER_INT_STAT);
    TIC12400_ProbeStoreTransaction(&interrupt_status);
    g_tic12400_probe.last_int_status = interrupt_status.data;
    if (interrupt_status.data != 0U)
    {
        g_tic12400_probe.last_nonzero_int_status =
            interrupt_status.data;
    }
    if ((interrupt_status.data & TIC12400_INT_STATUS_SSC) != 0U)
    {
        g_tic12400_probe.ssc_events++;
    }
    if (interrupt_status.result != TIC12400_RESULT_OK)
    {
        TIC12400_ProbeRecordServiceFailure(&interrupt_status);
        return;
    }

    comparator_status = TIC12400_ReadRegister(
        &tic12400_device,
        TIC12400_REGISTER_IN_STAT_COMP);
    TIC12400_ProbeStoreTransaction(&comparator_status);
    if (comparator_status.result != TIC12400_RESULT_OK)
    {
        TIC12400_ProbeRecordServiceFailure(&comparator_status);
        return;
    }

    previous_comparator_bitmap =
        g_tic12400_probe.comparator_bitmap;
    g_tic12400_probe.comparator_bitmap =
        comparator_status.data & TIC12400_BOARD_FITTED_INPUT_MASK;
    g_tic12400_probe.closed_switch_bitmap =
        (~g_tic12400_probe.comparator_bitmap) &
        TIC12400_BOARD_FITTED_INPUT_MASK;
    g_tic12400_probe.in0_above_threshold =
        ((g_tic12400_probe.comparator_bitmap & 1U) != 0U) ?
        1U : 0U;
    g_tic12400_probe.in0_closed =
        ((g_tic12400_probe.closed_switch_bitmap & 1U) != 0U) ?
        1U : 0U;
    g_tic12400_probe.switch_samples++;

    if (g_tic12400_probe.baseline_established != 0U)
    {
        changed_inputs =
            (previous_comparator_bitmap ^
             g_tic12400_probe.comparator_bitmap) &
            TIC12400_BOARD_FITTED_INPUT_MASK;
        if (changed_inputs != 0U)
        {
            g_tic12400_probe.last_change_mask = changed_inputs;
            g_tic12400_probe.switch_changes +=
                TIC12400_ProbeCountBits(changed_inputs);
        }
    }

    g_tic12400_probe.baseline_established = 1U;
    g_tic12400_probe.service_result = TIC12400_RESULT_OK;
}

static void TIC12400_ProbeConfigureInputs(void)
{
    g_tic12400_probe.configuration_result = TIC12400_RESULT_OK;
    g_tic12400_probe.enabled_input_mask =
        TIC12400_BOARD_FITTED_INPUT_MASK;

    /*
     * The reset defaults already select comparator mode and a current source,
     * but write and verify them explicitly so the debugger proves the complete
     * fitted-input test configuration.
     */
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_CONFIG,
            0U,
            &g_tic12400_probe.config_readback) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_MODE,
            0U,
            &g_tic12400_probe.mode_readback) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_CS_SELECT,
            0U,
            &g_tic12400_probe.cs_select_readback) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_WC_CFG0,
            TIC12400_ALL_WC_CFG0_1_MA,
            &g_tic12400_probe.wc_cfg0_readback) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_WC_CFG1,
            TIC12400_ALL_WC_CFG1_1_MA,
            &g_tic12400_probe.wc_cfg1_readback) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_IN_EN,
            TIC12400_BOARD_FITTED_INPUT_MASK,
            &g_tic12400_probe.in_en_readback) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_INT_EN_COMP1,
            TIC12400_COMP1_BOTH_EDGES,
            &g_tic12400_probe.int_en_comp1_readback) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_INT_EN_COMP2,
            TIC12400_COMP2_BOTH_EDGES_NO_IN12,
            &g_tic12400_probe.int_en_comp2_readback) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_INT_EN_CFG0,
            TIC12400_INT_ENABLE_SSC,
            &g_tic12400_probe.int_en_cfg0_readback) == 0U)
    {
        return;
    }
    if (TIC12400_ProbeRunConfigurationCrc() == 0U)
    {
        return;
    }
    if (TIC12400_ProbeWriteAndVerify(
            TIC12400_REGISTER_CONFIG,
            TIC12400_CONFIG_TRIGGER,
            &g_tic12400_probe.config_readback) == 0U)
    {
        return;
    }

    g_tic12400_probe.configuration_completed = 1U;
    g_tic12400_probe.configuration_passed = 1U;
    g_tic12400_probe.monitoring_started = 1U;

    /*
     * tSTARTUP is at most 400 us. Allow the first detection cycle to finish,
     * then preserve and acknowledge its expected baseline SSC interrupt.
     */
    HAL_Delay(2U);
    TIC12400_ProbeSampleInputs();
    tic12400_last_service_tick = HAL_GetTick();
}

void TIC12400_Probe_Init(SPI_HandleTypeDef *spi)
{
    uint32_t attempt;
    TIC12400_Result_t reset_result;
    TIC12400_Transaction_t transaction;
    TIC12400_Transaction_t interrupt_status;

    TIC12400_Driver_Init(&tic12400_device,
                         spi,
                         TIC12400_CS_GPIO_Port,
                         TIC12400_CS_Pin,
                         TIC12400_RESET_GPIO_Port,
                         TIC12400_RESET_Pin);

    attempt = g_tic12400_probe.attempts + 1U;
    g_tic12400_probe = (TIC12400_ProbeSnapshot_t){0};
    g_tic12400_probe.attempts = attempt;

    reset_result = TIC12400_HardwareReset(&tic12400_device);
    if (reset_result != TIC12400_RESULT_OK)
    {
        g_tic12400_probe.result = reset_result;
        return;
    }

    transaction = TIC12400_ReadDeviceId(&tic12400_device);
    g_tic12400_probe.first_rx_frame = transaction.rx_frame;
    g_tic12400_probe.first_result = transaction.result;
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
        g_tic12400_probe.recovery_attempted = 1U;
        interrupt_status = TIC12400_ReadRegister(
            &tic12400_device,
            TIC12400_REGISTER_INT_STAT);
        g_tic12400_probe.clear_rx_frame = interrupt_status.rx_frame;
        g_tic12400_probe.clear_result = interrupt_status.result;
        g_tic12400_probe.int_status = interrupt_status.data;
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
        if (transaction.result == TIC12400_RESULT_OK)
        {
            g_tic12400_probe.recovery_succeeded = 1U;
        }
    }

    if (transaction.result != TIC12400_RESULT_OK)
    {
        return;
    }

    /*
     * INT_STAT is clear-on-read at the rising edge of CS. Preserve its
     * power-on value in the debugger snapshot before clearing the device.
     */
    if (g_tic12400_probe.recovery_attempted == 0U)
    {
        interrupt_status = TIC12400_ReadRegister(
            &tic12400_device,
            TIC12400_REGISTER_INT_STAT);
        g_tic12400_probe.int_status = interrupt_status.data;
        if ((interrupt_status.data & 1U) != 0U)
        {
            g_tic12400_probe.por_observed = 1U;
        }
        if (interrupt_status.result != TIC12400_RESULT_OK)
        {
            TIC12400_ProbeStoreTransaction(&interrupt_status);
            return;
        }
        g_tic12400_probe.interrupt_pending = 0U;
    }

    TIC12400_ProbeValidateCommunication();
    if (g_tic12400_probe.validation_passed != 0U)
    {
        g_tic12400_probe.online = 1U;
        TIC12400_ProbeConfigureInputs();
    }
}

void TIC12400_Probe_Process(void)
{
    uint32_t now;

    if (g_tic12400_probe.monitoring_started == 0U)
    {
        return;
    }

    now = HAL_GetTick();
    if ((g_tic12400_probe.interrupt_pending == 0U) &&
        ((now - tic12400_last_service_tick) <
         TIC12400_PROBE_FALLBACK_POLL_MS))
    {
        return;
    }

    TIC12400_ProbeSampleInputs();
    tic12400_last_service_tick = now;
}

void TIC12400_Probe_NotifyInterruptFromIsr(void)
{
    g_tic12400_probe.interrupt_count++;
    g_tic12400_probe.interrupt_pending = 1U;
}

void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin)
{
    if (gpio_pin == TIC12400_INT_Pin)
    {
        TIC12400_Probe_NotifyInterruptFromIsr();
    }
}
