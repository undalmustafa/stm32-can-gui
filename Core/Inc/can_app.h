#ifndef CAN_APP_H
#define CAN_APP_H

#include <stdint.h>

#define CAN_APP_RX_FRAME_BUDGET_PER_PROCESS 8U

typedef enum
{
    CAN_APP_RX_REJECT_NONE = 0,
    CAN_APP_RX_REJECT_WRONG_ID,
    CAN_APP_RX_REJECT_FRAME_FORMAT,
    CAN_APP_RX_REJECT_DLC,
    CAN_APP_RX_REJECT_UNKNOWN_COMMAND,
    CAN_APP_RX_REJECT_INVALID_PAYLOAD,
    CAN_APP_RX_REJECT_HAL_ERROR,
    CAN_APP_RX_REJECT_ACCESS_DENIED
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
    uint32_t rejected_access_denied;
    uint32_t command_acks_sent;
    uint32_t command_ack_tx_failures;
    uint32_t hal_rx_errors;
    uint32_t rx_budget_hits;
    CAN_App_RxRejectReason_t last_reject_reason;
    uint8_t last_rejected_command;
} CAN_App_RxStats_t;

typedef enum
{
    CAN_APP_INIT_OK = 0,
    CAN_APP_INIT_PERIPHERAL_UNAVAILABLE,
    CAN_APP_INIT_FILTER_ERROR,
    CAN_APP_INIT_GLOBAL_FILTER_ERROR,
    CAN_APP_INIT_START_ERROR,
    CAN_APP_INIT_NOTIFICATION_ERROR
} CAN_App_InitResult_t;

typedef struct
{
    uint32_t init_attempts;
    uint32_t process_count;
    CAN_App_InitResult_t init_result;
    uint32_t hal_error;
    uint8_t available;
} CAN_App_State_t;

/* Convenient for STM32CubeIDE Live Expressions. */
extern volatile CAN_App_State_t g_canAppState;

CAN_App_InitResult_t CAN_App_Init(uint8_t peripheral_ready);
void CAN_App_Process(void);
uint8_t CAN_App_IsAvailable(void);
CAN_App_State_t CAN_App_GetState(void);
void CAN_App_GetRxStats(CAN_App_RxStats_t *stats);
void CAN_App_RequestControlAccess(void);
void CAN_Process_Rx_Command(void);
void CAN_Process_TxSlots(void);
void CAN_Send_System_Status(void);
void System_Status_Process(void);

void CAN_Send_Pwm_Status(void);
void CAN_Send_Input_Capture_Status(void);
void CAN_Send_Pwm_Self_Test_Status(void);
void CAN_Send_Pwm_Self_Test_Result(void);

#endif /* CAN_APP_H */
