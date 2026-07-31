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
  volatile uint32_t PSR;
  volatile uint32_t ECR;
} FDCAN_GlobalTypeDef;

typedef struct
{
  FDCAN_GlobalTypeDef* Instance;
  uint32_t ErrorCode;
} FDCAN_HandleTypeDef;

typedef struct
{
  uint32_t IdType;
  uint32_t FilterIndex;
  uint32_t FilterType;
  uint32_t FilterConfig;
  uint32_t FilterID1;
  uint32_t FilterID2;
} FDCAN_FilterTypeDef;

extern FDCAN_GlobalTypeDef test_fdcan1_instance;
#define FDCAN1 (&test_fdcan1_instance)

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
#define FDCAN_FILTER_MASK              0x00000001U
#define FDCAN_FILTER_TO_RXFIFO0        0x00000002U
#define FDCAN_REJECT                   0x00000003U
#define FDCAN_REJECT_REMOTE            0x00000004U
#define FDCAN_RX_FIFO0                 0x00000000U
#define FDCAN_RX_FIFO_BLOCKING         0x00000000U
#define FDCAN_CFG_RX_FIFO0             0x00000000U
#define FDCAN_IT_ERROR_PASSIVE          (1UL << 0)
#define FDCAN_IT_BUS_OFF                (1UL << 1)
#define FDCAN_IT_TX_COMPLETE            (1UL << 2)
#define FDCAN_IT_RX_FIFO0_NEW_MESSAGE   (1UL << 3)
#define FDCAN_IT_RX_FIFO0_WATERMARK     (1UL << 4)
#define FDCAN_IT_RX_FIFO0_FULL          (1UL << 5)
#define FDCAN_IT_RX_FIFO0_MESSAGE_LOST  (1UL << 6)

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
extern HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef *hfdcan);
extern HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *hfdcan);
extern HAL_StatusTypeDef HAL_FDCAN_ConfigFilter(
    FDCAN_HandleTypeDef *hfdcan,
    FDCAN_FilterTypeDef *sFilterConfig);
extern HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(
    FDCAN_HandleTypeDef *hfdcan,
    uint32_t NonMatchingStd,
    uint32_t NonMatchingExt,
    uint32_t RejectRemoteStd,
    uint32_t RejectRemoteExt);
extern HAL_StatusTypeDef HAL_FDCAN_ConfigRxFifoOverwrite(
    FDCAN_HandleTypeDef *hfdcan,
    uint32_t RxFifo,
    uint32_t OperationMode);
extern HAL_StatusTypeDef HAL_FDCAN_ConfigFifoWatermark(
    FDCAN_HandleTypeDef *hfdcan,
    uint32_t Fifo,
    uint32_t Watermark);
extern HAL_StatusTypeDef HAL_FDCAN_ActivateNotification(
    FDCAN_HandleTypeDef *hfdcan,
    uint32_t ActiveITs,
    uint32_t BufferIndexes);

#endif /* STM32H7XX_HAL_STUB_H */
