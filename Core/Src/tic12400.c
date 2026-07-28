#include "tic12400.h"

#include <stddef.h>

#define TIC12400_SPI_TIMEOUT_MS            10U
#define TIC12400_CS_SETUP_DELAY_US          1U
#define TIC12400_CS_HOLD_DELAY_US           1U
#define TIC12400_INTERFRAME_DELAY_US        5U
#define TIC12400_FRAME_WRITE_MASK          (1UL << 31)
#define TIC12400_FRAME_ADDRESS_SHIFT       25U
#define TIC12400_FRAME_DATA_SHIFT          1U
#define TIC12400_FRAME_DATA_MASK           TIC12400_REGISTER_DATA_MAX

#define TIC12400_STATUS_POR_MASK           (1UL << 31)
#define TIC12400_STATUS_SPI_FAIL_MASK      (1UL << 30)
#define TIC12400_STATUS_PARITY_FAIL_MASK   (1UL << 29)
#define TIC12400_STATUS_SSC_MASK           (1UL << 28)
#define TIC12400_STATUS_SUPPLY_MASK        (1UL << 27)
#define TIC12400_STATUS_TEMPERATURE_MASK   (1UL << 26)
#define TIC12400_STATUS_OTHER_MASK         (1UL << 25)

static uint8_t TIC12400_CountOnes(uint32_t value)
{
    uint8_t count = 0U;

    while (value != 0U)
    {
        count = (uint8_t)(count + (uint8_t)(value & 1U));
        value >>= 1U;
    }

    return count;
}

static TIC12400_Transaction_t TIC12400_CreateTransaction(
    TIC12400_Result_t result)
{
    TIC12400_Transaction_t transaction = {0};

    transaction.result = result;
    transaction.hal_status = HAL_OK;
    return transaction;
}

static void TIC12400_PackFrame(uint32_t frame, uint8_t bytes[4])
{
    bytes[0] = (uint8_t)(frame >> 24);
    bytes[1] = (uint8_t)(frame >> 16);
    bytes[2] = (uint8_t)(frame >> 8);
    bytes[3] = (uint8_t)frame;
}

static uint32_t TIC12400_UnpackFrame(const uint8_t bytes[4])
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static void TIC12400_DecodeStatus(
    uint32_t frame,
    TIC12400_StatusFlags_t *status)
{
    status->spi_fail =
        ((frame & TIC12400_STATUS_SPI_FAIL_MASK) != 0U) ? 1U : 0U;
    status->parity_fail =
        ((frame & TIC12400_STATUS_PARITY_FAIL_MASK) != 0U) ? 1U : 0U;
    status->switch_state_change =
        ((frame & TIC12400_STATUS_SSC_MASK) != 0U) ? 1U : 0U;
    status->supply_threshold =
        ((frame & TIC12400_STATUS_SUPPLY_MASK) != 0U) ? 1U : 0U;
    status->temperature =
        ((frame & TIC12400_STATUS_TEMPERATURE_MASK) != 0U) ? 1U : 0U;
    status->other_interrupt =
        ((frame & TIC12400_STATUS_OTHER_MASK) != 0U) ? 1U : 0U;
    status->power_on_reset =
        ((frame & TIC12400_STATUS_POR_MASK) != 0U) ? 1U : 0U;
}

static void TIC12400_WaitMicroseconds(uint32_t microseconds)
{
#if defined(DWT) && defined(CoreDebug) && \
    defined(DWT_CTRL_CYCCNTENA_Msk)
    uint32_t cycles_per_us;
    uint32_t required_cycles;
    uint32_t start;

    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->LAR = 0xC5ACCE55UL;
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }

    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
    {
        /*
         * The STM32H7 implements CYCCNT. Retain a safe, bounded fallback
         * instead of risking a permanent wait if trace access is unavailable.
         */
        HAL_Delay(1U);
        return;
    }

    cycles_per_us = (SystemCoreClock + 999999UL) / 1000000UL;
    required_cycles = cycles_per_us * microseconds;
    start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < required_cycles)
    {
        /* Busy wait for the requested TIC12400 transaction margin. */
    }
#else
    (void)microseconds;
#endif
}

static TIC12400_Transaction_t TIC12400_ExchangeFrame(
    const TIC12400_Device_t *device,
    uint32_t tx_frame)
{
    TIC12400_Transaction_t transaction;
    uint8_t tx_bytes[4];
    uint8_t rx_bytes[4] = {0};

    if ((device == NULL) ||
        (device->spi == NULL) ||
        (device->chip_select_port == NULL) ||
        (device->chip_select_pin == 0U))
    {
        return TIC12400_CreateTransaction(
            TIC12400_RESULT_INVALID_ARGUMENT);
    }

    transaction = TIC12400_CreateTransaction(TIC12400_RESULT_OK);
    transaction.tx_frame = tx_frame;
    TIC12400_PackFrame(transaction.tx_frame, tx_bytes);

    HAL_GPIO_WritePin(device->chip_select_port,
                      device->chip_select_pin,
                      GPIO_PIN_RESET);
    TIC12400_WaitMicroseconds(TIC12400_CS_SETUP_DELAY_US);
    transaction.hal_status = HAL_SPI_TransmitReceive(
        device->spi,
        tx_bytes,
        rx_bytes,
        4U,
        TIC12400_SPI_TIMEOUT_MS);
    TIC12400_WaitMicroseconds(TIC12400_CS_HOLD_DELAY_US);
    HAL_GPIO_WritePin(device->chip_select_port,
                      device->chip_select_pin,
                      GPIO_PIN_SET);
    TIC12400_WaitMicroseconds(TIC12400_INTERFRAME_DELAY_US);

    if (transaction.hal_status != HAL_OK)
    {
        transaction.result = TIC12400_RESULT_HAL_ERROR;
        transaction.hal_error = HAL_SPI_GetError(device->spi);
        return transaction;
    }

    transaction.rx_frame = TIC12400_UnpackFrame(rx_bytes);
    transaction.data =
        (transaction.rx_frame >> TIC12400_FRAME_DATA_SHIFT) &
        TIC12400_FRAME_DATA_MASK;
    TIC12400_DecodeStatus(transaction.rx_frame, &transaction.status);

    if (TIC12400_FrameHasOddParity(transaction.rx_frame) == 0U)
    {
        transaction.result = TIC12400_RESULT_RESPONSE_PARITY_ERROR;
    }
    else if (transaction.status.spi_fail != 0U)
    {
        transaction.result = TIC12400_RESULT_DEVICE_SPI_ERROR;
    }
    else if (transaction.status.parity_fail != 0U)
    {
        transaction.result = TIC12400_RESULT_DEVICE_PARITY_ERROR;
    }

    return transaction;
}

void TIC12400_Driver_Init(TIC12400_Device_t *device,
                          SPI_HandleTypeDef *spi,
                          GPIO_TypeDef *chip_select_port,
                          uint16_t chip_select_pin,
                          GPIO_TypeDef *reset_port,
                          uint16_t reset_pin)
{
    if (device == NULL)
    {
        return;
    }

    device->spi = spi;
    device->chip_select_port = chip_select_port;
    device->chip_select_pin = chip_select_pin;
    device->reset_port = reset_port;
    device->reset_pin = reset_pin;
}

uint8_t TIC12400_FrameHasOddParity(uint32_t frame)
{
    return ((TIC12400_CountOnes(frame) & 1U) != 0U) ? 1U : 0U;
}

uint32_t TIC12400_BuildReadFrame(uint8_t register_address)
{
    uint32_t frame;

    if (register_address > TIC12400_REGISTER_ADDRESS_MAX)
    {
        return 0U;
    }

    frame = (uint32_t)register_address <<
            TIC12400_FRAME_ADDRESS_SHIFT;

    if (TIC12400_FrameHasOddParity(frame) == 0U)
    {
        frame |= 1U;
    }

    return frame;
}

uint32_t TIC12400_BuildWriteFrame(uint8_t register_address,
                                 uint32_t register_data)
{
    uint32_t frame;

    if ((register_address > TIC12400_REGISTER_ADDRESS_MAX) ||
        (register_data > TIC12400_FRAME_DATA_MASK))
    {
        return 0U;
    }

    frame = TIC12400_FRAME_WRITE_MASK |
            ((uint32_t)register_address <<
             TIC12400_FRAME_ADDRESS_SHIFT) |
            (register_data << TIC12400_FRAME_DATA_SHIFT);

    if (TIC12400_FrameHasOddParity(frame) == 0U)
    {
        frame |= 1U;
    }

    return frame;
}

TIC12400_Result_t TIC12400_HardwareReset(
    const TIC12400_Device_t *device)
{
    if ((device == NULL) ||
        (device->reset_port == NULL) ||
        (device->reset_pin == 0U))
    {
        return TIC12400_RESULT_INVALID_ARGUMENT;
    }

    /*
     * RESET is active high. Two milliseconds covers the data-sheet 2 us
     * minimum and the up-to-1 ms completion time when an interrupt is pending.
     */
    HAL_GPIO_WritePin(device->reset_port,
                      device->reset_pin,
                      GPIO_PIN_SET);
    HAL_Delay(2U);
    HAL_GPIO_WritePin(device->reset_port,
                      device->reset_pin,
                      GPIO_PIN_RESET);
    HAL_Delay(2U);

    return TIC12400_RESULT_OK;
}

TIC12400_Transaction_t TIC12400_ReadRegister(
    const TIC12400_Device_t *device,
    uint8_t register_address)
{
    if (register_address > TIC12400_REGISTER_ADDRESS_MAX)
    {
        return TIC12400_CreateTransaction(
            TIC12400_RESULT_INVALID_ADDRESS);
    }

    return TIC12400_ExchangeFrame(
        device,
        TIC12400_BuildReadFrame(register_address));
}

TIC12400_Transaction_t TIC12400_WriteRegister(
    const TIC12400_Device_t *device,
    uint8_t register_address,
    uint32_t register_data)
{
    if (register_address > TIC12400_REGISTER_ADDRESS_MAX)
    {
        return TIC12400_CreateTransaction(
            TIC12400_RESULT_INVALID_ADDRESS);
    }

    if (register_data > TIC12400_FRAME_DATA_MASK)
    {
        return TIC12400_CreateTransaction(
            TIC12400_RESULT_INVALID_DATA);
    }

    return TIC12400_ExchangeFrame(
        device,
        TIC12400_BuildWriteFrame(register_address, register_data));
}

TIC12400_Transaction_t TIC12400_ReadDeviceId(
    const TIC12400_Device_t *device)
{
    TIC12400_Transaction_t transaction =
        TIC12400_ReadRegister(device, TIC12400_REGISTER_DEVICE_ID);

    if ((transaction.result == TIC12400_RESULT_OK) &&
        (transaction.data != TIC12400_EXPECTED_DEVICE_ID))
    {
        transaction.result = TIC12400_RESULT_DEVICE_ID_MISMATCH;
    }

    return transaction;
}

TIC12400_Transaction_t TIC12400_ReadConfigurationCrc(
    const TIC12400_Device_t *device)
{
    return TIC12400_ReadRegister(device, TIC12400_REGISTER_CRC);
}

TIC12400_Transaction_t TIC12400_ReadAdcPair(
    const TIC12400_Device_t *device,
    uint8_t pair_index)
{
    if (pair_index >= TIC12400_ADC_PAIR_COUNT)
    {
        return TIC12400_CreateTransaction(
            TIC12400_RESULT_INVALID_ADDRESS);
    }

    return TIC12400_ReadRegister(
        device,
        (uint8_t)(TIC12400_REGISTER_ANA_STAT0 + pair_index));
}

void TIC12400_DecodeAdcPair(uint32_t register_data,
                            uint16_t *first_code,
                            uint16_t *second_code)
{
    if ((first_code == NULL) || (second_code == NULL))
    {
        return;
    }

    *first_code =
        (uint16_t)(register_data & TIC12400_ADC_CODE_MASK);
    *second_code = (uint16_t)(
        (register_data >> TIC12400_ADC_SECOND_CODE_SHIFT) &
        TIC12400_ADC_CODE_MASK);
}
