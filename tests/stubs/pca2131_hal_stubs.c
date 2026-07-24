#include "stm32h7xx_hal.h"

#include <string.h>

HAL_StatusTypeDef HAL_I2C_IsDeviceReady(
    I2C_HandleTypeDef *hi2c,
    uint16_t device_address,
    uint32_t trials,
    uint32_t timeout)
{
    (void)hi2c;
    (void)device_address;
    (void)trials;
    (void)timeout;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Read(
    I2C_HandleTypeDef *hi2c,
    uint16_t device_address,
    uint16_t memory_address,
    uint16_t memory_address_size,
    uint8_t *data,
    uint16_t size,
    uint32_t timeout)
{
    (void)hi2c;
    (void)device_address;
    (void)memory_address;
    (void)memory_address_size;
    (void)timeout;

    if (data != NULL)
    {
        memset(data, 0, size);
    }

    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Write(
    I2C_HandleTypeDef *hi2c,
    uint16_t device_address,
    uint16_t memory_address,
    uint16_t memory_address_size,
    uint8_t *data,
    uint16_t size,
    uint32_t timeout)
{
    (void)hi2c;
    (void)device_address;
    (void)memory_address;
    (void)memory_address_size;
    (void)data;
    (void)size;
    (void)timeout;
    return HAL_OK;
}

uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *hi2c)
{
    (void)hi2c;
    return 0U;
}
