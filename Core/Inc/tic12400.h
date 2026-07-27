#ifndef TIC12400_H
#define TIC12400_H

#include "stm32h7xx_hal.h"

#include <stdint.h>

#define TIC12400_REGISTER_DEVICE_ID       0x01U
#define TIC12400_REGISTER_INT_STAT        0x02U
#define TIC12400_EXPECTED_DEVICE_ID       0x000020U
#define TIC12400_REGISTER_ADDRESS_MAX     0x3FU

typedef enum
{
    TIC12400_RESULT_OK = 0,
    TIC12400_RESULT_INVALID_ARGUMENT,
    TIC12400_RESULT_INVALID_ADDRESS,
    TIC12400_RESULT_HAL_ERROR,
    TIC12400_RESULT_RESPONSE_PARITY_ERROR,
    TIC12400_RESULT_DEVICE_SPI_ERROR,
    TIC12400_RESULT_DEVICE_PARITY_ERROR,
    TIC12400_RESULT_DEVICE_ID_MISMATCH
} TIC12400_Result_t;

typedef struct
{
    uint8_t spi_fail;
    uint8_t parity_fail;
    uint8_t switch_state_change;
    uint8_t supply_threshold;
    uint8_t temperature;
    uint8_t other_interrupt;
    uint8_t power_on_reset;
} TIC12400_StatusFlags_t;

typedef struct
{
    TIC12400_Result_t result;
    HAL_StatusTypeDef hal_status;
    uint32_t hal_error;
    uint32_t tx_frame;
    uint32_t rx_frame;
    uint32_t data;
    TIC12400_StatusFlags_t status;
} TIC12400_Transaction_t;

typedef struct
{
    SPI_HandleTypeDef *spi;
    GPIO_TypeDef *chip_select_port;
    uint16_t chip_select_pin;
    GPIO_TypeDef *reset_port;
    uint16_t reset_pin;
} TIC12400_Device_t;

void TIC12400_Driver_Init(TIC12400_Device_t *device,
                          SPI_HandleTypeDef *spi,
                          GPIO_TypeDef *chip_select_port,
                          uint16_t chip_select_pin,
                          GPIO_TypeDef *reset_port,
                          uint16_t reset_pin);

uint8_t TIC12400_FrameHasOddParity(uint32_t frame);
uint32_t TIC12400_BuildReadFrame(uint8_t register_address);

TIC12400_Result_t TIC12400_HardwareReset(
    const TIC12400_Device_t *device);

TIC12400_Transaction_t TIC12400_ReadRegister(
    const TIC12400_Device_t *device,
    uint8_t register_address);

TIC12400_Transaction_t TIC12400_ReadDeviceId(
    const TIC12400_Device_t *device);

#endif /* TIC12400_H */
