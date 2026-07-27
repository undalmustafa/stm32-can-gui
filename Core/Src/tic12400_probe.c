#include "tic12400_probe.h"

#include "main.h"

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

void TIC12400_Probe_Init(SPI_HandleTypeDef *spi)
{
    TIC12400_Result_t reset_result;
    TIC12400_Transaction_t transaction;
    TIC12400_Transaction_t interrupt_status;

    TIC12400_Driver_Init(&tic12400_device,
                         spi,
                         TIC12400_CS_GPIO_Port,
                         TIC12400_CS_Pin,
                         TIC12400_RESET_GPIO_Port,
                         TIC12400_RESET_Pin);

    g_tic12400_probe.attempts++;
    g_tic12400_probe.online = 0U;
    g_tic12400_probe.por_observed = 0U;

    reset_result = TIC12400_HardwareReset(&tic12400_device);
    if (reset_result != TIC12400_RESULT_OK)
    {
        g_tic12400_probe.result = reset_result;
        return;
    }

    transaction = TIC12400_ReadDeviceId(&tic12400_device);
    TIC12400_ProbeStoreTransaction(&transaction);
    g_tic12400_probe.device_id = (uint8_t)transaction.data;
    g_tic12400_probe.por_observed =
        transaction.status.power_on_reset;

    if (transaction.result != TIC12400_RESULT_OK)
    {
        return;
    }

    g_tic12400_probe.online = 1U;

    /*
     * INT_STAT is clear-on-read at the rising edge of CS. Preserve its
     * power-on value in the debugger snapshot before clearing the device.
     */
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
    }
    else
    {
        g_tic12400_probe.interrupt_pending = 0U;
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
