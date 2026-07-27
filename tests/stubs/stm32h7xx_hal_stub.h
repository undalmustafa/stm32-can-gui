#ifndef STM32H7XX_HAL_STUB_H
#define STM32H7XX_HAL_STUB_H

#include <stdint.h>
#include <stddef.h>

/* Basic HAL types */
typedef enum 
{
  HAL_OK       = 0x00U,
  HAL_ERROR    = 0x01U,
  HAL_BUSY     = 0x02U,
  HAL_TIMEOUT  = 0x03U
} HAL_StatusTypeDef;

/* I2C Types */
typedef struct __I2C_HandleTypeDef
{
  void* Instance; 
} I2C_HandleTypeDef;

typedef struct __SPI_HandleTypeDef
{
  void* Instance;
  uint32_t ErrorCode;
} SPI_HandleTypeDef;

typedef struct
{
  uint32_t unused;
} GPIO_TypeDef;

typedef enum
{
  GPIO_PIN_RESET = 0U,
  GPIO_PIN_SET
} GPIO_PinState;

#define I2C_MEMADD_SIZE_8BIT 0x00000001U

/* FDCAN Types */
typedef struct
{
  uint32_t Identifier;
  uint32_t IdType;
  uint32_t TxFrameType;
  uint32_t DataLength;
  uint32_t ErrorStateIndicator;
  uint32_t BitRateSwitch;
  uint32_t FDFormat;
  uint32_t TxEventFifoControl;
  uint32_t MessageMarker;
} FDCAN_TxHeaderTypeDef;

typedef struct
{
  uint32_t Identifier;
  uint32_t IdType;
  uint32_t RxFrameType;
  uint32_t DataLength;
  uint32_t ErrorStateIndicator;
  uint32_t BitRateSwitch;
  uint32_t FDFormat;
  uint32_t RxTimestamp;
  uint32_t FilterIndex;
  uint32_t IsFilterMatchingFrame;
} FDCAN_RxHeaderTypeDef;

typedef struct
{
  void* Instance;
} FDCAN_HandleTypeDef;

#define FDCAN_STANDARD_ID 0x00000000U
#define FDCAN_EXTENDED_ID 0x40000000U
#define FDCAN_DATA_FRAME  0x00000000U
#define FDCAN_DLC_BYTES_8 0x00000008U
#define FDCAN_ESI_ACTIVE  0x00000000U
#define FDCAN_BRS_OFF     0x00000000U
#define FDCAN_CLASSIC_CAN 0x00000000U
#define FDCAN_NO_TX_EVENTS 0x00000000U
#define FDCAN_TX_BUFFER0  0x00000001U
#define FDCAN_TX_BUFFER1  0x00000002U
#define FDCAN_TX_BUFFER2  0x00000004U

/* Mock HAL Functions used in our source */
extern uint32_t HAL_GetTick(void);
extern HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint32_t Trials, uint32_t Timeout);
extern HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint16_t MemAddress, uint16_t MemAddSize, uint8_t *pData, uint16_t Size, uint32_t Timeout);
extern HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c, uint16_t DevAddress, uint16_t MemAddress, uint16_t MemAddSize, uint8_t *pData, uint16_t Size, uint32_t Timeout);
extern uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *hi2c);
extern void HAL_Delay(uint32_t Delay);
extern void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin,
                              GPIO_PinState PinState);
extern HAL_StatusTypeDef HAL_SPI_TransmitReceive(
    SPI_HandleTypeDef *hspi,
    const uint8_t *pTxData,
    uint8_t *pRxData,
    uint16_t Size,
    uint32_t Timeout);
extern uint32_t HAL_SPI_GetError(const SPI_HandleTypeDef *hspi);
extern uint32_t HAL_FDCAN_GetTxFifoFreeLevel(FDCAN_HandleTypeDef *hfdcan);
extern HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *hfdcan, FDCAN_TxHeaderTypeDef *pTxHeader, uint8_t *pTxData);

#endif /* STM32H7XX_HAL_STUB_H */
