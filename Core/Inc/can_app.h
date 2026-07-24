#ifndef CAN_APP_H
#define CAN_APP_H

#include <stdint.h>

typedef enum
{
    CAN_APP_RX_REJECT_NONE = 0,
    CAN_APP_RX_REJECT_WRONG_ID,
    CAN_APP_RX_REJECT_FRAME_FORMAT,
    CAN_APP_RX_REJECT_DLC,
    CAN_APP_RX_REJECT_UNKNOWN_COMMAND,
    CAN_APP_RX_REJECT_INVALID_PAYLOAD,
    CAN_APP_RX_REJECT_HAL_ERROR
} CAN_App_RxRejectReason_t;

typedef struct
{
    uint32_t frames_received;
    uint32_t commands_accepted;
    uint32_t rejected_wrong_id;
    uint32_t rejected_frame_format;
    uint32_t rejected_dlc;
    uint32_t rejected_unknown_command;
    uint32_t rejected_invalid_payload;
    uint32_t hal_rx_errors;
    CAN_App_RxRejectReason_t last_reject_reason;
    uint8_t last_rejected_command;
} CAN_App_RxStats_t;

void CAN_App_Init(void);
void CAN_App_Process(void);
void CAN_App_GetRxStats(CAN_App_RxStats_t *stats);
void CAN_Process_Rx_Command(void);
void CAN_Process_TxSlots(void);
void CAN_Send_System_Status(void);
void System_Status_Process(void);

#endif /* CAN_APP_H */
