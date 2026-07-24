#include "main.h"
#include "can_app.h"
#include "can_protocol.h"
#include "can_command_guard.h"
#include "can_transport.h"
#include "can_recovery.h"
#include "rtc_app.h"
#include "app_diagnostics.h"
#include "app_log.h"
#include "app_watchdog.h"
#include "app_log_can.h"
#include "pwm_control.h"
#include "input_capture.h"
#include "pwm_self_test.h"
#define SYSTEM_STATUS_PERIOD_MS         500U
#define CAN_CONTROL_ACCESS_WINDOW_MS    240000UL

extern FDCAN_HandleTypeDef hfdcan1;

typedef struct
{
    uint8_t configured;
    uint8_t active;
    uint8_t running;

    uint8_t id_type;          // 0 = Standard, 1 = Extended
    uint32_t can_id;

    uint16_t cycle_time_ms;

    uint32_t counter_limit;   // Kullanıcının GUI'de girdiği son sayı
    uint32_t counter_value;   // 1, 2, 3 ... counter_limit

    uint32_t last_tx_time;
} CAN_TxSlot_t;

static CAN_TxSlot_t tx_slot_1;
static CAN_TxSlot_t tx_slot_2;

static FDCAN_RxHeaderTypeDef RxHeader;
static uint8_t RxData[8];
static uint8_t led1_state = 0U;
static uint8_t led2_state = 0U;
static CAN_App_RxStats_t can_rx_stats;
static uint32_t pwm_self_test_result_sequence_sent;
static CAN_CommandGuard_t command_guard;
static volatile uint8_t control_access_requested;
static uint8_t control_access_active;
static uint32_t control_access_deadline;

typedef enum
{
    CAN_COMMAND_VALID = 0,
    CAN_COMMAND_UNKNOWN,
    CAN_COMMAND_INVALID_PAYLOAD
} CAN_CommandValidationResult_t;

static void CAN_Process_TxSlot(CAN_TxSlot_t *slot);
static void CAN_Handle_LED_Command(uint8_t *data);
static void CAN_Record_Rx_Reject(CAN_App_RxRejectReason_t reason,
                                 uint8_t command);
static uint8_t CAN_Is_Control_Access_Open(void);
static void CAN_Send_Command_Ack(
    uint8_t command,
    uint8_t sequence,
    CAN_Protocol_CommandAckStatus_t status,
    uint8_t flags);

static CAN_CommandValidationResult_t CAN_Validate_Command(
    const uint8_t data[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    uint8_t id_type;
    uint32_t can_id;
    CAN_Protocol_RtcAlarmCommand_t alarm_command;

    switch (data[0])
    {
        case CAN_PROTOCOL_CMD_SET_SLOT_1:
        case CAN_PROTOCOL_CMD_SET_SLOT_2:

            /* Flags içinde yalnızca bit0 ve bit1 kullanılabilir. */
            if ((data[1] & 0xFCU) != 0U)
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            id_type = ((data[1] & CAN_PROTOCOL_SLOT_FLAG_EXTENDED_ID) != 0U)
                    ? 1U
                    : 0U;

            can_id = CAN_Protocol_ReadU32LE(&data[2]);

            if (CAN_Protocol_IsValidId(id_type, can_id) == 0U)
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            /* Cycle time sıfır olamaz. */
            if (CAN_Protocol_ReadU16LE(&data[6]) == 0U)
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            return CAN_COMMAND_VALID;

        case CAN_PROTOCOL_CMD_START_SLOT_1_COUNTER:
        case CAN_PROTOCOL_CMD_START_SLOT_2_COUNTER:

            /* Kullanılmayan baytların sıfır olması bekleniyor. */
            if ((data[1] != 0U) ||
                (data[6] != 0U) ||
                (data[7] != 0U))
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            return CAN_COMMAND_VALID;

        case CAN_PROTOCOL_CMD_LED_CONTROL:

            if ((data[1] < 1U) || (data[1] > 2U))
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            if (data[2] > 1U)
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            if ((data[3] != 0U) ||
                (data[4] != 0U) ||
                (data[5] != 0U) ||
                (data[6] != 0U) ||
                (data[7] != 0U))
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            return CAN_COMMAND_VALID;

        case CAN_PROTOCOL_CMD_RTC_SET_TIME:

            if ((data[1] > 23U) ||
                (data[2] > 59U) ||
                (data[3] > 59U))
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            if ((data[4] != 0U) ||
                (data[5] != 0U) ||
                (data[6] != 0U) ||
                (data[7] != 0U))
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            return CAN_COMMAND_VALID;

        case CAN_PROTOCOL_CMD_RTC_SET_DATETIME:

            if (PCA2131_Is_Valid_DateTime(
                    data[1],
                    data[2],
                    data[3],
                    data[4],
                    data[5],
                    (data[6] >> 5) & 0x07U,
                    data[6] & 0x1FU,
                    data[7]) == 0U)
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            return CAN_COMMAND_VALID;

        case CAN_PROTOCOL_CMD_RTC_SET_ALARM:

            if (CAN_Protocol_DecodeRtcAlarmCommand(
                    data, &alarm_command) == 0U)
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            return CAN_COMMAND_VALID;

        case CAN_PROTOCOL_CMD_LOG_GET_INFO:

            if ((data[1] != 0U) ||
                (data[2] != 0U) ||
                (data[3] != 0U) ||
                (data[4] != 0U) ||
                (data[5] != 0U) ||
                (data[6] != 0U) ||
                (data[7] != 0U))
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            return CAN_COMMAND_VALID;

        case CAN_PROTOCOL_CMD_LOG_READ_SEQUENCE:

            if ((CAN_Protocol_ReadU32LE(&data[1]) == 0U) ||
                (data[5] != 0U) ||
                (data[6] != 0U) ||
                (data[7] != 0U))
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            return CAN_COMMAND_VALID;

        case CAN_PROTOCOL_CMD_PWM_SET:
        {
            uint32_t freq = CAN_Protocol_ReadU32LE(&data[1]);

            if ((freq != 0U) &&
                ((freq < 1U) || (freq > 1000000U)))
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            if (data[5] > 100U)
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            if ((data[6] != 0U) || (data[7] != 0U))
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            return CAN_COMMAND_VALID;
        }

        case CAN_PROTOCOL_CMD_PWM_SELF_TEST:

            if ((data[1] > 1U) ||
                (data[2] != 0U) ||
                (data[3] != 0U) ||
                (data[4] != 0U) ||
                (data[5] != 0U) ||
                (data[6] != 0U) ||
                (data[7] != 0U))
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            return CAN_COMMAND_VALID;

        case CAN_PROTOCOL_CMD_SESSION_START:

            if ((CAN_Protocol_ReadU32LE(&data[1]) == 0U) ||
                (data[5] != CAN_PROTOCOL_VERSION) ||
                (data[6] != 0U) ||
                (data[7] != 0U))
            {
                return CAN_COMMAND_INVALID_PAYLOAD;
            }

            return CAN_COMMAND_VALID;

        default:
            return CAN_COMMAND_UNKNOWN;
    }
}

static void CAN_Record_Rx_Reject(CAN_App_RxRejectReason_t reason,
                                 uint8_t command)
{
    uint32_t detail = 0U;
    App_Log_Severity_t severity = APP_LOG_SEVERITY_WARNING;

    can_rx_stats.last_reject_reason = reason;
    can_rx_stats.last_rejected_command = command;

    switch (reason)
    {
        case CAN_APP_RX_REJECT_WRONG_ID:
            can_rx_stats.rejected_wrong_id++;
            detail = RxHeader.Identifier;
            break;

        case CAN_APP_RX_REJECT_FRAME_FORMAT:
            can_rx_stats.rejected_frame_format++;
            detail =
                ((RxHeader.RxFrameType == FDCAN_REMOTE_FRAME) ? 1UL : 0UL) |
                ((RxHeader.FDFormat == FDCAN_FD_CAN) ? (1UL << 1) : 0UL);
            break;

        case CAN_APP_RX_REJECT_DLC:
            can_rx_stats.rejected_dlc++;
            detail = RxHeader.DataLength;
            break;

        case CAN_APP_RX_REJECT_UNKNOWN_COMMAND:
            can_rx_stats.rejected_unknown_command++;
            detail = command;
            break;

        case CAN_APP_RX_REJECT_INVALID_PAYLOAD:
            can_rx_stats.rejected_invalid_payload++;
            detail = command;
            break;

        case CAN_APP_RX_REJECT_REPLAY:
            can_rx_stats.rejected_replay++;
            detail = command;
            break;

        case CAN_APP_RX_REJECT_SESSION_REQUIRED:
            can_rx_stats.rejected_session_required++;
            detail = command;
            break;

        case CAN_APP_RX_REJECT_ACCESS_DENIED:
            can_rx_stats.rejected_access_denied++;
            detail = command;
            break;

        case CAN_APP_RX_REJECT_HAL_ERROR:
            can_rx_stats.hal_rx_errors++;
            detail = hfdcan1.ErrorCode;
            severity = APP_LOG_SEVERITY_FAULT;
            break;

        case CAN_APP_RX_REJECT_NONE:
        default:
            return;
    }

    /*
     * This function is called once for each actually rejected RX frame. Log
     * here, in main-loop context, instead of polling cumulative diagnostics;
     * therefore the same reject is never written again every 100 ms.
     *
     * data_0: CAN_App_RxRejectReason_t
     * data_1: reason-specific detail (ID, frame format, raw DLC, command or
     *         HAL error code)
     */
    (void)App_Log_Push(APP_LOG_SOURCE_CAN,
                       severity,
                       APP_LOG_EVENT_CAN_RX_REJECTED,
                       (uint32_t)reason,
                       detail);
}

void CAN_App_RequestControlAccess(void)
{
    control_access_requested = 1U;
}

static uint8_t CAN_Is_Control_Access_Open(void)
{
    uint32_t now = HAL_GetTick();

    if (control_access_requested != 0U)
    {
        control_access_requested = 0U;
        control_access_active = 1U;
        control_access_deadline = now + CAN_CONTROL_ACCESS_WINDOW_MS;
        (void)App_Log_Push(
            APP_LOG_SOURCE_CAN,
            APP_LOG_SEVERITY_INFO,
            APP_LOG_EVENT_CAN_CONTROL_ACCESS_OPENED,
            CAN_CONTROL_ACCESS_WINDOW_MS,
            control_access_deadline);
    }

    if ((control_access_active != 0U) &&
        ((int32_t)(control_access_deadline - now) > 0))
    {
        return 1U;
    }

    control_access_active = 0U;
    return 0U;
}

static void CAN_Send_Command_Ack(
    uint8_t command,
    uint8_t sequence,
    CAN_Protocol_CommandAckStatus_t status,
    uint8_t flags)
{
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE] = {0};
    uint32_t now = HAL_GetTick();
    uint32_t remaining_ms = 0U;
    CAN_Transport_Result_t result;

    if (CAN_Is_Control_Access_Open() != 0U)
    {
        flags |= CAN_PROTOCOL_COMMAND_ACK_FLAG_ACCESS_OPEN;
        remaining_ms = control_access_deadline - now;
    }

    payload[0] = CAN_PROTOCOL_VERSION;
    payload[1] = command;
    payload[2] = sequence;
    payload[3] = (uint8_t)status;
    payload[4] = flags;
    payload[5] = (remaining_ms == 0U)
               ? 0U
               : (uint8_t)(((remaining_ms + 999U) / 1000U) > 255U
                         ? 255U
                         : ((remaining_ms + 999U) / 1000U));
    payload[6] = (uint8_t)(
        (RxHeader.Identifier & CAN_PROTOCOL_GUI_COMMAND_SESSION_MASK) >>
        CAN_PROTOCOL_GUI_COMMAND_SESSION_SHIFT);

    result = CAN_Transport_SendClassicHighPriority(
        CAN_PROTOCOL_COMMAND_ACK_TX_ID,
        CAN_TRANSPORT_ID_STANDARD,
        payload);

    if ((result == CAN_TRANSPORT_OK) ||
        (result == CAN_TRANSPORT_QUEUED))
    {
        can_rx_stats.command_acks_sent++;
    }
    else
    {
        can_rx_stats.command_ack_tx_failures++;
    }
}

static void CAN_Reset_Slot(CAN_TxSlot_t *slot)
{
    slot->configured = 0;
    slot->active = 0;
    slot->running = 0;
    slot->id_type = 0;
    slot->can_id = 0;
    slot->cycle_time_ms = 0;
    slot->counter_limit = 0;
    slot->counter_value = 0;
    slot->last_tx_time = 0;
}

static void CAN_Set_Slot_Config(CAN_TxSlot_t *slot, uint8_t *data)
{
    uint8_t flags;
    uint8_t id_type;
    uint32_t can_id;
    uint16_t cycle_time_ms;

    flags = data[1];

    if ((flags & CAN_PROTOCOL_SLOT_FLAG_EXTENDED_ID) != 0U)
    {
        id_type = 1U;
    }
    else
    {
        id_type = 0U;
    }

    can_id = CAN_Protocol_ReadU32LE(&data[2]);
    cycle_time_ms = CAN_Protocol_ReadU16LE(&data[6]);

    if (cycle_time_ms == 0U)
    {
        cycle_time_ms = 1U;
    }

    if (CAN_Protocol_IsValidId(id_type, can_id) == 0U)
    {
        slot->active = 0U;
        slot->running = 0U;
        return;
    }

    slot->configured = 1U;

    if ((flags & CAN_PROTOCOL_SLOT_FLAG_ENABLE) != 0U)
    {
        slot->active = 1U;
    }
    else
    {
        slot->active = 0U;
        slot->running = 0U;
    }

    slot->id_type = id_type;
    slot->can_id = can_id;
    slot->cycle_time_ms = cycle_time_ms;

    /*
     Yeni config gelince mevcut sayma durdurulur.
     Counter/start komutu gelince tekrar 1'den başlar.
    */
    slot->running = 0U;
    slot->counter_value = 0U;
}

static void CAN_Start_Slot_Counter(CAN_TxSlot_t *slot, uint8_t *data)
{
    uint32_t counter_limit;

    counter_limit = CAN_Protocol_ReadU32LE(&data[2]);

    slot->counter_limit = counter_limit;

    if ((slot->configured == 0U) || (slot->active == 0U))
    {
        slot->running = 0U;
        return;
    }

    if (counter_limit == 0U)
    {
        slot->running = 0U;
        slot->counter_value = 0U;
        return;
    }

    /*
     Kullanıcının istediği mantık:
     t = 0 ms      -> 1 gönder
     t = 50 ms     -> 2 gönder
     t = 100 ms    -> 3 gönder

     Bu yüzden counter_value 1'den başlatılır.
    */
    slot->counter_value = 1U;
    slot->running = 1U;

    /*
     İlk mesajın hemen gönderilebilmesi için last_tx_time geçmişe çekilir.
    */
    slot->last_tx_time = HAL_GetTick() - slot->cycle_time_ms;
}

void CAN_App_Init(void)
{
    FDCAN_FilterTypeDef sFilterConfig;

    can_rx_stats = (CAN_App_RxStats_t){0};
    CAN_CommandGuard_Init(&command_guard);
    control_access_requested = 0U;
    control_access_active = 0U;
    control_access_deadline = 0U;

    CAN_Transport_Init(&hfdcan1);
    App_Log_Can_Init();

    CAN_Reset_Slot(&tx_slot_1);
    CAN_Reset_Slot(&tx_slot_2);

    /*
     GUI'den gelen komut mesajı:
     Extended ID = 0x1894AABB
     Bu mesaj RX FIFO0'a alınacak.
    */
    sFilterConfig.IdType = FDCAN_EXTENDED_ID;
    sFilterConfig.FilterIndex = 0;
    sFilterConfig.FilterType = FDCAN_FILTER_MASK;
    sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    sFilterConfig.FilterID1 =
        CAN_PROTOCOL_GUI_COMMAND_ID_EXT &
        CAN_PROTOCOL_GUI_COMMAND_ID_MASK_EXT;
    sFilterConfig.FilterID2 =
        CAN_PROTOCOL_GUI_COMMAND_ID_MASK_EXT;

    if (HAL_FDCAN_ConfigFilter(&hfdcan1, &sFilterConfig) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_FDCAN_ConfigGlobalFilter(&hfdcan1,
                                     FDCAN_REJECT,
                                     FDCAN_REJECT,
                                     FDCAN_REJECT_REMOTE,
                                     FDCAN_REJECT_REMOTE) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK)
    {
        Error_Handler();
    }

    /* USER CODE BEGIN CAN_App_Init BusOff */
    /* Enable bus-off, error-passive and TX-complete notifications. Recovery
       is accepted only after a real transmission completes on the bus.
       This requires the FDCAN1 interrupt (NVIC: FDCAN1_IT0_IRQn) to be
       enabled in CubeMX and stm32h7xx_it.c to forward it to
       HAL_FDCAN_IRQHandler - see notes below. */
    if (CAN_Recovery_EnableNotifications() != HAL_OK)
    {
        Error_Handler();
    }
    /* USER CODE END CAN_App_Init BusOff */

    App_Diagnostics_Init();
}

void CAN_App_GetRxStats(CAN_App_RxStats_t *stats)
{
    if (stats != NULL)
    {
        *stats = can_rx_stats;
    }
}

void CAN_App_Process(void)
{
    /* Önce önceki döngüden kalan mesajları ilerlet. */
    CAN_Handle_BusOff_Recovery();
    CAN_Transport_Process();

    /* Uygulama servisleri yeni mesajlar üretebilir. */
    CAN_Process_Rx_Command();
    RTC_Process();
    Input_Capture_Process();
    PWM_SelfTest_Process();
    CAN_Send_Pwm_Self_Test_Result();
    App_Watchdog_CheckIn(APP_WATCHDOG_HEARTBEAT_RTC_SERVICE);
    System_Status_Process();
    App_Log_Can_Process();
    CAN_Process_TxSlots();

    /* Bu döngüde oluşan mesajları bekletmeden ilerlet. */
    CAN_Transport_Process();
    App_Diagnostics_Process();
    App_Watchdog_CheckIn(APP_WATCHDOG_HEARTBEAT_CAN_APP);
}

void CAN_Process_Rx_Command(void)
{
    CAN_CommandValidationResult_t validation_result;
    CAN_CommandGuardDecision_t guard_decision;
    uint8_t command;
    uint8_t sequence;
    uint8_t session_tag;
    uint8_t ack_flags;
    uint32_t processed_count = 0U;

    while ((processed_count < CAN_APP_RX_FRAME_BUDGET_PER_PROCESS) &&
           (HAL_FDCAN_GetRxFifoFillLevel(
                &hfdcan1, FDCAN_RX_FIFO0) > 0U))
    {
        if (HAL_FDCAN_GetRxMessage(&hfdcan1,
                                   FDCAN_RX_FIFO0,
                                   &RxHeader,
                                   RxData) != HAL_OK)
        {
            CAN_Record_Rx_Reject(CAN_APP_RX_REJECT_HAL_ERROR, 0U);

            /* FIFO seviyesi değişmediyse aynı hatada sonsuz döngüye girme. */
            break;
        }

        processed_count++;
        can_rx_stats.frames_received++;

        if ((RxHeader.IdType != FDCAN_EXTENDED_ID) ||
            ((RxHeader.Identifier &
              CAN_PROTOCOL_GUI_COMMAND_ID_MASK_EXT) !=
             (CAN_PROTOCOL_GUI_COMMAND_ID_EXT &
              CAN_PROTOCOL_GUI_COMMAND_ID_MASK_EXT)))
        {
            CAN_Record_Rx_Reject(CAN_APP_RX_REJECT_WRONG_ID, 0U);
            continue;
        }

        /* Komut protokolü yalnızca Classic CAN data frame kabul eder. */
        if ((RxHeader.RxFrameType != FDCAN_DATA_FRAME) ||
            (RxHeader.FDFormat != FDCAN_CLASSIC_CAN))
        {
            CAN_Record_Rx_Reject(CAN_APP_RX_REJECT_FRAME_FORMAT, 0U);
            continue;
        }

        if (RxHeader.DataLength != FDCAN_DLC_BYTES_8)
        {
            CAN_Record_Rx_Reject(CAN_APP_RX_REJECT_DLC, 0U);
            continue;
        }

        command = RxData[0];
        sequence = (uint8_t)(
            RxHeader.Identifier &
            CAN_PROTOCOL_GUI_COMMAND_SEQUENCE_MASK);
        session_tag = (uint8_t)(
            (RxHeader.Identifier &
             CAN_PROTOCOL_GUI_COMMAND_SESSION_MASK) >>
            CAN_PROTOCOL_GUI_COMMAND_SESSION_SHIFT);
        validation_result = CAN_Validate_Command(RxData);

        if (command == CAN_PROTOCOL_CMD_SESSION_START)
        {
            if ((validation_result != CAN_COMMAND_VALID) ||
                (session_tag != (uint8_t)
                    CAN_Protocol_ReadU32LE(&RxData[1])))
            {
                CAN_Record_Rx_Reject(CAN_APP_RX_REJECT_INVALID_PAYLOAD,
                                     command);
                CAN_Send_Command_Ack(
                    command,
                    sequence,
                    (RxData[5] != CAN_PROTOCOL_VERSION)
                        ? CAN_PROTOCOL_COMMAND_ACK_PROTOCOL_MISMATCH
                        : CAN_PROTOCOL_COMMAND_ACK_INVALID_PAYLOAD,
                    0U);
                continue;
            }

            guard_decision = CAN_CommandGuard_StartSession(
                &command_guard,
                CAN_Protocol_ReadU32LE(&RxData[1]),
                session_tag,
                sequence,
                RxData);

            if (guard_decision == CAN_COMMAND_GUARD_DUPLICATE)
            {
                can_rx_stats.duplicate_commands++;
                CAN_Send_Command_Ack(
                    command,
                    sequence,
                    CAN_PROTOCOL_COMMAND_ACK_DUPLICATE,
                    CAN_PROTOCOL_COMMAND_ACK_FLAG_SESSION_STARTED);
                continue;
            }

            if (guard_decision != CAN_COMMAND_GUARD_ACCEPT)
            {
                CAN_Record_Rx_Reject(CAN_APP_RX_REJECT_REPLAY, command);
                CAN_Send_Command_Ack(
                    command,
                    sequence,
                    CAN_PROTOCOL_COMMAND_ACK_REPLAY_REJECTED,
                    0U);
                continue;
            }

            CAN_Send_Command_Ack(
                command,
                sequence,
                CAN_PROTOCOL_COMMAND_ACK_ACCEPTED,
                CAN_PROTOCOL_COMMAND_ACK_FLAG_SESSION_STARTED);
            continue;
        }

        /*
         * Evaluate without consuming the sequence so invalid or unauthorized
         * commands cannot later look like successfully executed duplicates.
         */
        guard_decision = CAN_CommandGuard_Evaluate(
            &command_guard,
            session_tag,
            sequence,
            RxData);

        if (guard_decision == CAN_COMMAND_GUARD_SESSION_REQUIRED)
        {
            CAN_Record_Rx_Reject(CAN_APP_RX_REJECT_SESSION_REQUIRED,
                                 command);
            CAN_Send_Command_Ack(
                command,
                sequence,
                CAN_PROTOCOL_COMMAND_ACK_SESSION_REQUIRED,
                0U);
            continue;
        }

        if (guard_decision == CAN_COMMAND_GUARD_DUPLICATE)
        {
            can_rx_stats.duplicate_commands++;
            CAN_Send_Command_Ack(
                command,
                sequence,
                CAN_PROTOCOL_COMMAND_ACK_DUPLICATE,
                0U);
            continue;
        }

        if (guard_decision != CAN_COMMAND_GUARD_ACCEPT)
        {
            CAN_Record_Rx_Reject(CAN_APP_RX_REJECT_REPLAY, command);
            CAN_Send_Command_Ack(
                command,
                sequence,
                CAN_PROTOCOL_COMMAND_ACK_REPLAY_REJECTED,
                0U);
            continue;
        }

        if (validation_result == CAN_COMMAND_UNKNOWN)
        {
            CAN_Record_Rx_Reject(CAN_APP_RX_REJECT_UNKNOWN_COMMAND,
                                 command);
            CAN_Send_Command_Ack(
                command,
                sequence,
                CAN_PROTOCOL_COMMAND_ACK_UNKNOWN_COMMAND,
                0U);
            continue;
        }

        if (validation_result == CAN_COMMAND_INVALID_PAYLOAD)
        {
            CAN_Record_Rx_Reject(CAN_APP_RX_REJECT_INVALID_PAYLOAD,
                                 command);
            CAN_Send_Command_Ack(
                command,
                sequence,
                CAN_PROTOCOL_COMMAND_ACK_INVALID_PAYLOAD,
                0U);
            continue;
        }

        if ((CAN_CommandGuard_IsPrivileged(RxData) != 0U) &&
            (CAN_Is_Control_Access_Open() == 0U))
        {
            CAN_Record_Rx_Reject(CAN_APP_RX_REJECT_ACCESS_DENIED,
                                 command);
            CAN_Send_Command_Ack(
                command,
                sequence,
                CAN_PROTOCOL_COMMAND_ACK_ACCESS_DENIED,
                0U);
            continue;
        }

        /*
         * The non-mutating evaluation above returned ACCEPT, so this call
         * records the sequence immediately before command dispatch.
         */
        guard_decision = CAN_CommandGuard_Check(
            &command_guard,
            session_tag,
            sequence,
            RxData);
        if (guard_decision != CAN_COMMAND_GUARD_ACCEPT)
        {
            CAN_Record_Rx_Reject(CAN_APP_RX_REJECT_REPLAY, command);
            CAN_Send_Command_Ack(
                command,
                sequence,
                CAN_PROTOCOL_COMMAND_ACK_REPLAY_REJECTED,
                0U);
            continue;
        }

        can_rx_stats.commands_accepted++;
        ack_flags = CAN_PROTOCOL_COMMAND_ACK_FLAG_EXECUTED;
        CAN_Send_Command_Ack(
            command,
            sequence,
            CAN_PROTOCOL_COMMAND_ACK_ACCEPTED,
            ack_flags);

        switch (command)
        {
            case CAN_PROTOCOL_CMD_SET_SLOT_1:
                CAN_Set_Slot_Config(&tx_slot_1, RxData);
                CAN_Send_System_Status();
                break;

            case CAN_PROTOCOL_CMD_SET_SLOT_2:
                CAN_Set_Slot_Config(&tx_slot_2, RxData);
                CAN_Send_System_Status();
                break;

            case CAN_PROTOCOL_CMD_START_SLOT_1_COUNTER:
                CAN_Start_Slot_Counter(&tx_slot_1, RxData);
                CAN_Send_System_Status();
                break;

            case CAN_PROTOCOL_CMD_START_SLOT_2_COUNTER:
                CAN_Start_Slot_Counter(&tx_slot_2, RxData);
                CAN_Send_System_Status();
                break;

            case CAN_PROTOCOL_CMD_LED_CONTROL:
                CAN_Handle_LED_Command(RxData);
                CAN_Send_System_Status();
                break;

            case CAN_PROTOCOL_CMD_RTC_SET_TIME:
                CAN_Handle_RTC_Set_Time(RxData);
                break;

            case CAN_PROTOCOL_CMD_RTC_SET_DATETIME:
                CAN_Handle_RTC_Set_DateTime(RxData);
                break;

            case CAN_PROTOCOL_CMD_RTC_SET_ALARM:
                CAN_Handle_RTC_Set_Alarm(RxData);
                break;

            case CAN_PROTOCOL_CMD_LOG_GET_INFO:
                App_Log_Can_SendInfo();
                break;

            case CAN_PROTOCOL_CMD_LOG_READ_SEQUENCE:
                App_Log_Can_SendRecord(
                    CAN_Protocol_ReadU32LE(&RxData[1]));
                break;

            case CAN_PROTOCOL_CMD_PWM_SET:
            {
                uint32_t freq = CAN_Protocol_ReadU32LE(&RxData[1]);
                uint8_t duty = RxData[5];

                if (PWM_SelfTest_IsRunning() != 0U)
                {
                    PWM_SelfTest_Cancel();
                }

                if (freq == 0U)
                {
                    PWM_Control_Stop();
                }
                else
                {
                    if (PWM_Control_Set(freq, duty) == PWM_CONTROL_OK)
                    {
                        (void)PWM_Control_Start();
                    }
                }

                CAN_Send_Pwm_Status();
                break;
            }

            case CAN_PROTOCOL_CMD_PWM_SELF_TEST:
                if (RxData[1] == 0U)
                {
                    PWM_SelfTest_Cancel();
                }
                else
                {
                    (void)PWM_SelfTest_Start();
                }
                CAN_Send_Pwm_Self_Test_Status();
                break;

            default:
                break;
        }
    }

    if ((processed_count == CAN_APP_RX_FRAME_BUDGET_PER_PROCESS) &&
        (HAL_FDCAN_GetRxFifoFillLevel(
            &hfdcan1, FDCAN_RX_FIFO0) > 0U))
    {
        can_rx_stats.rx_budget_hits++;
    }
}

static void CAN_Process_TxSlot(CAN_TxSlot_t *slot)
{
    uint32_t now;
    CAN_Transport_IdType_t id_type;
    CAN_Transport_Result_t send_result;
    uint8_t txData[8] = {0};

    if (slot->running == 0U)
    {
        return;
    }

    if (slot->counter_limit == 0U)
    {
        slot->running = 0U;
        slot->counter_value = 0U;
        return;
    }

    if (slot->counter_value == 0U)
    {
        slot->counter_value = 1U;
    }

    if (slot->counter_value > slot->counter_limit)
    {
        slot->counter_value = 1U;
    }

    now = HAL_GetTick();

    if ((now - slot->last_tx_time) >= slot->cycle_time_ms)
    {
        slot->last_tx_time = now;

        if (slot->id_type == 0U)
        {
            id_type = CAN_TRANSPORT_ID_STANDARD;
        }
        else
        {
            id_type = CAN_TRANSPORT_ID_EXTENDED;
        }

        /*
         Gönderilen mesaj payload'u:
         Data[0..3] = counter_value, uint32 little endian
         Data[4..7] = 0
        */
        CAN_Protocol_WriteU32LE(&txData[0], slot->counter_value);

        send_result = CAN_Transport_SendClassicLatest(slot->can_id,
                                                       id_type,
                                                       txData);

        if ((send_result == CAN_TRANSPORT_OK) ||
            (send_result == CAN_TRANSPORT_QUEUED))
        {
            /*
             Yeni mantık:
             Counter limit'e ulaşınca slot durmaz.
             Tekrar 1'den başlar.
            */
            if (slot->counter_value >= slot->counter_limit)
            {
                slot->counter_value = 1U;
            }
            else
            {
                slot->counter_value++;
            }
        }
    }
}
static void CAN_Handle_LED_Command(uint8_t *data)
{
    uint8_t led_no = data[1];
    uint8_t led_state = data[2];

    if (led_no == 1U)
    {
        if (led_state == 1U)
        {
            led1_state = 1U;
            BSP_LED_On(LED_GREEN);
        }
        else
        {
            led1_state = 0U;
            BSP_LED_Off(LED_GREEN);
        }
    }
    else if (led_no == 2U)
    {
        if (led_state == 1U)
        {
            led2_state = 1U;
            BSP_LED_On(LED_RED);
        }
        else
        {
            led2_state = 0U;
            BSP_LED_Off(LED_RED);
        }
    }
}
void System_Status_Process(void)
{
    static uint32_t last_system_status_time = 0U;
    uint32_t now = HAL_GetTick();

    if ((now - last_system_status_time) >= SYSTEM_STATUS_PERIOD_MS)
    {
        last_system_status_time = now;
        CAN_Send_System_Status();
        CAN_Send_Pwm_Status();
        CAN_Send_Input_Capture_Status();
        CAN_Send_Pwm_Self_Test_Status();
    }
}

void CAN_Send_Pwm_Self_Test_Status(void)
{
    CAN_Protocol_PwmSelfTestStatus_t status;
    PWM_SelfTest_State_t state = PWM_SelfTest_GetState();
    uint8_t txData[8] = {0};

    status.state = (uint8_t)state.state;
    status.current_point = state.current_point;
    status.total_points = state.total_points;
    status.passed_points = state.passed_points;
    status.expected_frequency_hz = state.expected_frequency_hz;
    CAN_Protocol_EncodePwmSelfTestStatus(&status, txData);

    (void)CAN_Transport_SendClassicLatest(
        CAN_PROTOCOL_PWM_SELF_TEST_STATUS_TX_ID,
        CAN_TRANSPORT_ID_STANDARD,
        txData);
}

void CAN_Send_Pwm_Self_Test_Result(void)
{
    CAN_Protocol_PwmSelfTestResult_t result;
    PWM_SelfTest_State_t state = PWM_SelfTest_GetState();
    uint8_t txData[8] = {0};

    if (state.result_sequence == pwm_self_test_result_sequence_sent)
    {
        return;
    }

    result.point = state.last_result.point;
    result.passed = state.last_result.passed;
    result.expected_duty_percent =
        state.last_result.expected_duty_percent;
    result.measured_duty_percent =
        state.last_result.measured_duty_percent;
    result.measured_frequency_hz =
        state.last_result.measured_frequency_hz;
    CAN_Protocol_EncodePwmSelfTestResult(&result, txData);

    if (CAN_Transport_SendClassicHighPriority(
            CAN_PROTOCOL_PWM_SELF_TEST_RESULT_TX_ID,
            CAN_TRANSPORT_ID_STANDARD,
            txData) != CAN_TRANSPORT_QUEUE_FULL)
    {
        pwm_self_test_result_sequence_sent = state.result_sequence;
    }
}

void CAN_Send_Input_Capture_Status(void)
{
    CAN_Protocol_InputCaptureStatus_t status;
    Input_Capture_State_t state = Input_Capture_GetState();
    uint8_t txData[8] = {0};

    status.signal_detected = state.signal_detected;
    status.duty_percent = state.duty_percent;
    status.frequency_hz = state.frequency_hz;
    status.edge_count = (uint16_t)state.edge_count;
    CAN_Protocol_EncodeInputCaptureStatus(&status, txData);

    (void)CAN_Transport_SendClassicLatest(
        CAN_PROTOCOL_INPUT_CAPTURE_STATUS_TX_ID,
        CAN_TRANSPORT_ID_STANDARD,
        txData);
}
void CAN_Send_System_Status(void)
{
    CAN_Protocol_SystemStatus_t system_status;
    uint8_t txData[8] = {0};

    system_status.slot_1_running = tx_slot_1.running;
    system_status.slot_2_running = tx_slot_2.running;
    system_status.led_1_on = led1_state;
    system_status.led_2_on = led2_state;

    CAN_Protocol_EncodeSystemStatus(&system_status, txData);

    (void)CAN_Transport_SendClassicLatest(CAN_PROTOCOL_SYSTEM_STATUS_TX_ID,
                                          CAN_TRANSPORT_ID_STANDARD,
                                          txData);
}

void CAN_Send_Pwm_Status(void)
{
    CAN_Protocol_PwmStatus_t pwm_status;
    uint8_t txData[8] = {0};
    PWM_Control_State_t state = PWM_Control_GetState();

    pwm_status.running = state.running;
    pwm_status.duty_percent = state.duty_percent;
    pwm_status.actual_frequency_hz = state.actual_frequency_hz;

    CAN_Protocol_EncodePwmStatus(&pwm_status, txData);

    (void)CAN_Transport_SendClassicLatest(CAN_PROTOCOL_PWM_STATUS_TX_ID,
                                          CAN_TRANSPORT_ID_STANDARD,
                                          txData);
}

void CAN_Process_TxSlots(void)
{
    CAN_Process_TxSlot(&tx_slot_1);
    CAN_Process_TxSlot(&tx_slot_2);
}

void BSP_PB_Callback(Button_TypeDef Button)
{
    if (Button == BUTTON_USER)
    {
        CAN_App_RequestControlAccess();
    }
}
