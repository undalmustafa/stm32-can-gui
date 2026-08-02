#include "can_app_init.h"

#include "app_log.h"
#include "can_protocol.h"
#include "can_recovery.h"
#include "can_transport.h"

#include <stddef.h>

volatile CAN_App_State_t g_canAppState;

static CAN_App_InitResult_t CAN_App_RecordInitFailure(
    FDCAN_HandleTypeDef *hfdcan,
    CAN_App_InitResult_t result)
{
    g_canAppState.available = 0U;
    g_canAppState.init_result = result;
    g_canAppState.hal_error = (hfdcan == NULL) ? 0U : hfdcan->ErrorCode;

    CAN_Transport_Init(NULL);
    (void)App_Log_Push(
        APP_LOG_SOURCE_CAN,
        APP_LOG_SEVERITY_FAULT,
        APP_LOG_EVENT_CAN_STARTUP_FAILED,
        (uint32_t)result,
        g_canAppState.hal_error);

    return result;
}

CAN_App_InitResult_t CAN_App_InitHardware(FDCAN_HandleTypeDef *hfdcan,
                                          uint8_t peripheral_ready)
{
    FDCAN_FilterTypeDef filter = {0};

    g_canAppState = (CAN_App_State_t){0};
    g_canAppState.init_attempts = 1U;
    CAN_Recovery_Init();
    CAN_Transport_Init(NULL);

    if ((peripheral_ready == 0U) || (hfdcan == NULL))
    {
        return CAN_App_RecordInitFailure(
            hfdcan,
            CAN_APP_INIT_PERIPHERAL_UNAVAILABLE);
    }

    CAN_Transport_Init(hfdcan);

    /* Accept the fixed extended GUI command identifier. */
    filter.IdType = FDCAN_EXTENDED_ID;
    filter.FilterIndex = 0U;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = CAN_PROTOCOL_GUI_COMMAND_ID_EXT;
    filter.FilterID2 = 0x1FFFFFFFUL;

    if (HAL_FDCAN_ConfigFilter(hfdcan, &filter) != HAL_OK)
    {
        return CAN_App_RecordInitFailure(
            hfdcan,
            CAN_APP_INIT_FILTER_ERROR);
    }

    /* Accept the physical ISO-TP request identifier on the standard bus. */
    filter = (FDCAN_FilterTypeDef){0};
    filter.IdType = FDCAN_STANDARD_ID;
    filter.FilterIndex = 0U;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = CAN_PROTOCOL_DIAGNOSTIC_REQUEST_RX_ID;
    filter.FilterID2 = 0x7FFU;

    if (HAL_FDCAN_ConfigFilter(hfdcan, &filter) != HAL_OK)
    {
        return CAN_App_RecordInitFailure(
            hfdcan,
            CAN_APP_INIT_FILTER_ERROR);
    }

    if (HAL_FDCAN_ConfigGlobalFilter(hfdcan,
                                     FDCAN_REJECT,
                                     FDCAN_REJECT,
                                     FDCAN_REJECT_REMOTE,
                                     FDCAN_REJECT_REMOTE) != HAL_OK)
    {
        return CAN_App_RecordInitFailure(
            hfdcan,
            CAN_APP_INIT_GLOBAL_FILTER_ERROR);
    }

    /* Preserve command ordering and reject excess traffic explicitly. */
    if (HAL_FDCAN_ConfigRxFifoOverwrite(
            hfdcan,
            FDCAN_RX_FIFO0,
            FDCAN_RX_FIFO_BLOCKING) != HAL_OK)
    {
        return CAN_App_RecordInitFailure(
            hfdcan,
            CAN_APP_INIT_FIFO_MODE_ERROR);
    }

    if (HAL_FDCAN_ConfigFifoWatermark(
            hfdcan,
            FDCAN_CFG_RX_FIFO0,
            CAN_APP_RX_FIFO0_WATERMARK) != HAL_OK)
    {
        return CAN_App_RecordInitFailure(
            hfdcan,
            CAN_APP_INIT_FIFO_WATERMARK_ERROR);
    }

    if (HAL_FDCAN_Start(hfdcan) != HAL_OK)
    {
        return CAN_App_RecordInitFailure(
            hfdcan,
            CAN_APP_INIT_START_ERROR);
    }

    if (CAN_Recovery_EnableNotifications(hfdcan) != HAL_OK)
    {
        (void)HAL_FDCAN_Stop(hfdcan);
        return CAN_App_RecordInitFailure(
            hfdcan,
            CAN_APP_INIT_NOTIFICATION_ERROR);
    }

    g_canAppState.available = 1U;
    g_canAppState.init_result = CAN_APP_INIT_OK;
    g_canAppState.hal_error = 0U;
    return CAN_APP_INIT_OK;
}

uint8_t CAN_App_IsAvailable(void)
{
    return g_canAppState.available;
}

CAN_App_State_t CAN_App_GetState(void)
{
    return (CAN_App_State_t)g_canAppState;
}
