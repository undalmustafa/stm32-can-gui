#include "tic12400_probe.h"

#include "main.h"

#define TIC12400_PROBE_VALIDATION_READS 1000U

volatile TIC12400_ProbeSnapshot_t g_tic12400_probe;

static TIC12400_Device_t tic12400_device;

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
    }
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
