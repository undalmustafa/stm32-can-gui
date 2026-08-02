#include "main.h"
#include "app_control.h"
#include "app_control_policy.h"
#include "can_app.h"
#include "can_app_init.h"
#include "can_protocol.h"
#include "can_command_guard.h"
#include "can_command_validation.h"
#include "can_control_access.h"
#include "can_transport.h"
#include "can_recovery.h"
#include "can_rx_health.h"
#include "rtc_app.h"
#include "app_diagnostics.h"
#include "app_log.h"
#include "app_watchdog.h"
#include "app_log_can.h"
#include "app_timing.h"
#include "pwm_control.h"
#include "input_capture.h"
#include "pwm_self_test.h"
#include "tic12400_probe.h"
#define SYSTEM_STATUS_PERIOD_MS         500U
#define CAN_TIMING_SERVICE_PERIOD_MS    100U
#define CAN_TIMING_ACK_PERIOD_MS        500U

extern FDCAN_HandleTypeDef hfdcan1;

typedef struct
{
    uint8_t configured;
    uint8_t active;
    uint8_t requested_running;
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
static CAN_App_RxStats_t can_rx_stats;
static uint32_t pwm_self_test_result_sequence_sent;
static CAN_ControlAccess_t control_access;
static uint32_t control_policy_update_applied;
static uint32_t control_output_update_applied;
static uint32_t timing_service_next_tick;
static uint32_t timing_ack_next_tick;
static uint8_t timing_service_index;

static void CAN_Process_TxSlot(CAN_TxSlot_t *slot);
static void CAN_Handle_LED_Command(uint8_t *data);
static void CAN_Apply_SlotPolicy(void);
static void CAN_Record_Rx_Reject(CAN_App_RxRejectReason_t reason,
                                 uint8_t command);
static void CAN_App_UpdateControlAccess(void);
static void CAN_Send_Command_Ack(
    uint8_t command,
    CAN_Protocol_CommandAckStatus_t status,
    uint8_t flags,
    uint32_t frame_start_cycles);
static void CAN_Send_Rx_Health(void);
static void CAN_Timing_Telemetry_Process(void);

static uint16_t CAN_SaturateU16(uint32_t value)
{
    return (value > UINT16_MAX) ? UINT16_MAX : (uint16_t)value;
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

void CAN_App_RequestControlAccessFromIsr(void)
{
    CAN_ControlAccess_RequestFromIsr(&control_access);
}

static void CAN_App_UpdateControlAccess(void)
{
    uint32_t now = HAL_GetTick();

    if (CAN_ControlAccess_Update(&control_access, now) ==
        CAN_CONTROL_ACCESS_OPENED)
    {
        (void)App_Log_Push(
            APP_LOG_SOURCE_CAN,
            APP_LOG_SEVERITY_INFO,
            APP_LOG_EVENT_CAN_CONTROL_ACCESS_OPENED,
            CAN_CONTROL_ACCESS_WINDOW_MS,
            control_access.deadline);
    }
}

static void CAN_Send_Command_Ack(
    uint8_t command,
    CAN_Protocol_CommandAckStatus_t status,
    uint8_t flags,
    uint32_t frame_start_cycles)
{
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE] = {0};
    uint32_t now = HAL_GetTick();
    uint32_t remaining_ms = 0U;
    CAN_Transport_Result_t result;

    remaining_ms =
        CAN_ControlAccess_RemainingMs(&control_access, now);
    if (remaining_ms != 0U)
    {
        flags |= CAN_PROTOCOL_COMMAND_ACK_FLAG_ACCESS_OPEN;
    }

    payload[0] = CAN_PROTOCOL_VERSION;
    payload[1] = command;
    payload[2] = CAN_Protocol_CalculateCommandToken(RxData);
    payload[3] = (uint8_t)status;
    payload[4] = flags;
    payload[5] = (remaining_ms == 0U)
               ? 0U
               : (uint8_t)(((remaining_ms + 999U) / 1000U) > 255U
                         ? 255U
                         : ((remaining_ms + 999U) / 1000U));
    payload[6] = 0U;
    payload[7] = 0U;

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

    App_Timing_RecordAckElapsed(
        frame_start_cycles,
        App_Timing_Now());
}

static void CAN_Reset_Slot(CAN_TxSlot_t *slot)
{
    slot->configured = 0;
    slot->active = 0;
    slot->requested_running = 0;
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
        slot->requested_running = 0U;
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
        slot->requested_running = 0U;
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
    slot->requested_running = 0U;
    slot->counter_value = 0U;
}

static void CAN_Start_Slot_Counter(
    CAN_TxSlot_t *slot,
    uint8_t slot_number,
    uint8_t *data)
{
    uint32_t counter_limit;

    counter_limit = CAN_Protocol_ReadU32LE(&data[2]);

    slot->counter_limit = counter_limit;

    if ((slot->configured == 0U) || (slot->active == 0U))
    {
        slot->requested_running = 0U;
        slot->running = 0U;
        App_ControlPolicy_SetSlotRequest(slot_number, 0U);
        return;
    }

    if (counter_limit == 0U)
    {
        slot->requested_running = 0U;
        slot->running = 0U;
        slot->counter_value = 0U;
        App_ControlPolicy_SetSlotRequest(slot_number, 0U);
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
    slot->requested_running = 1U;
    App_ControlPolicy_SetSlotRequest(slot_number, 1U);

    /*
     İlk mesajın hemen gönderilebilmesi için last_tx_time geçmişe çekilir.
    */
    slot->last_tx_time = HAL_GetTick() - slot->cycle_time_ms;
}

CAN_App_InitResult_t CAN_App_Init(uint8_t peripheral_ready)
{
    can_rx_stats = (CAN_App_RxStats_t){0};
    CAN_RxHealth_Init();
    CAN_ControlAccess_Init(&control_access);
    control_policy_update_applied = UINT32_MAX;
    control_output_update_applied = UINT32_MAX;
    timing_service_next_tick =
        HAL_GetTick() + CAN_TIMING_SERVICE_PERIOD_MS;
    timing_ack_next_tick = HAL_GetTick() + CAN_TIMING_ACK_PERIOD_MS;
    timing_service_index = 0U;

    App_Log_Can_Init();

    CAN_Reset_Slot(&tx_slot_1);
    CAN_Reset_Slot(&tx_slot_2);
    App_Diagnostics_Init();

    return CAN_App_InitHardware(&hfdcan1, peripheral_ready);
}

void CAN_App_GetRxStats(CAN_App_RxStats_t *stats)
{
    if (stats != NULL)
    {
        CAN_RxHealth_Stats_t health_stats;

        *stats = can_rx_stats;
        CAN_RxHealth_GetStats(&health_stats);
        stats->rx_new_message_events =
            health_stats.new_message_events;
        stats->rx_watermark_events = health_stats.watermark_events;
        stats->rx_full_events = health_stats.full_events;
        stats->rx_message_lost_events =
            health_stats.message_lost_events;
        stats->rx_max_fill_level = health_stats.max_fill_level;
    }
}

/*
 * ISR context. HAL clears the hardware flags before calling this hook. The
 * callback only maps flags, samples the fill level and updates counters;
 * frame reads, validation, logging and command execution remain in main.
 */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan,
                               uint32_t RxFifo0ITs)
{
    uint32_t events = 0U;
    uint32_t fill_level;

    if ((hfdcan == NULL) || (hfdcan->Instance != FDCAN1))
    {
        return;
    }

    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != 0U)
    {
        events |= CAN_RX_HEALTH_EVENT_NEW_MESSAGE;
    }
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_WATERMARK) != 0U)
    {
        events |= CAN_RX_HEALTH_EVENT_WATERMARK;
    }
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_FULL) != 0U)
    {
        events |= CAN_RX_HEALTH_EVENT_FULL;
    }
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_MESSAGE_LOST) != 0U)
    {
        events |= CAN_RX_HEALTH_EVENT_MESSAGE_LOST;
    }

    fill_level = HAL_FDCAN_GetRxFifoFillLevel(
        hfdcan, FDCAN_RX_FIFO0);
    CAN_RxHealth_RecordIsr(events, fill_level);
}

void CAN_App_Process(void)
{
    g_canAppState.process_count++;

    if (g_canAppState.available == 0U)
    {
        App_Diagnostics_Process();
        App_Watchdog_CheckIn(APP_WATCHDOG_HEARTBEAT_CAN_APP);
        return;
    }

    /* Önce önceki döngüden kalan mesajları ilerlet. */
    CAN_Handle_BusOff_Recovery();
    CAN_Transport_Process();

    /* Uygulama servisleri yeni mesajlar üretebilir. */
    CAN_Process_Rx_Command();
    CAN_Apply_SlotPolicy();
    CAN_Send_Pwm_Self_Test_Result();
    System_Status_Process();
    CAN_Timing_Telemetry_Process();
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
    uint8_t command;
    uint8_t ack_flags;
    uint32_t processed_count = 0U;
    uint32_t frame_start_cycles;

    CAN_App_UpdateControlAccess();

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
        frame_start_cycles = App_Timing_Begin();

        if ((RxHeader.IdType != FDCAN_EXTENDED_ID) ||
            (RxHeader.Identifier != CAN_PROTOCOL_GUI_COMMAND_ID_EXT))
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
        validation_result = CAN_CommandValidation_Validate(RxData);

        if (validation_result == CAN_COMMAND_VALIDATION_UNKNOWN)
        {
            CAN_Record_Rx_Reject(CAN_APP_RX_REJECT_UNKNOWN_COMMAND,
                                 command);
            CAN_Send_Command_Ack(
                command,
                CAN_PROTOCOL_COMMAND_ACK_UNKNOWN_COMMAND,
                0U,
                frame_start_cycles);
            continue;
        }

        if (validation_result == CAN_COMMAND_VALIDATION_INVALID_PAYLOAD)
        {
            CAN_Record_Rx_Reject(CAN_APP_RX_REJECT_INVALID_PAYLOAD,
                                 command);
            CAN_Send_Command_Ack(
                command,
                CAN_PROTOCOL_COMMAND_ACK_INVALID_PAYLOAD,
                0U,
                frame_start_cycles);
            continue;
        }

        CAN_App_UpdateControlAccess();
        if ((CAN_CommandGuard_IsPrivileged(RxData) != 0U) &&
            (CAN_ControlAccess_IsOpen(&control_access) == 0U))
        {
            CAN_Record_Rx_Reject(CAN_APP_RX_REJECT_ACCESS_DENIED,
                                 command);
            CAN_Send_Command_Ack(
                command,
                CAN_PROTOCOL_COMMAND_ACK_ACCESS_DENIED,
                0U,
                frame_start_cycles);
            continue;
        }

        can_rx_stats.commands_accepted++;
        ack_flags = CAN_PROTOCOL_COMMAND_ACK_FLAG_EXECUTED;

        switch (command)
        {
            case CAN_PROTOCOL_CMD_SET_SLOT_1:
                CAN_Set_Slot_Config(&tx_slot_1, RxData);
                App_ControlPolicy_SetSlotRequest(1U, 0U);
                break;

            case CAN_PROTOCOL_CMD_SET_SLOT_2:
                CAN_Set_Slot_Config(&tx_slot_2, RxData);
                App_ControlPolicy_SetSlotRequest(2U, 0U);
                break;

            case CAN_PROTOCOL_CMD_START_SLOT_1_COUNTER:
                CAN_Start_Slot_Counter(&tx_slot_1, 1U, RxData);
                break;

            case CAN_PROTOCOL_CMD_START_SLOT_2_COUNTER:
                CAN_Start_Slot_Counter(&tx_slot_2, 2U, RxData);
                break;

            case CAN_PROTOCOL_CMD_LED_CONTROL:
                CAN_Handle_LED_Command(RxData);
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
                    App_ControlPolicy_SetPwmRequest(0U);
                }
                else
                {
                    if (PWM_Control_Set(freq, duty) == PWM_CONTROL_OK)
                    {
                        App_ControlPolicy_SetPwmRequest(1U);
                    }
                }

                break;
            }

            case CAN_PROTOCOL_CMD_PWM_SELF_TEST:
                if (RxData[1] == 0U)
                {
                    PWM_SelfTest_Cancel();
                }
                else
                {
                    App_ControlPolicySnapshot_t policy =
                        App_ControlPolicy_GetSnapshot();
                    if (policy.pwm_permitted != 0U)
                    {
                        (void)PWM_SelfTest_Start();
                    }
                }
                CAN_Send_Pwm_Self_Test_Status();
                break;

            case CAN_PROTOCOL_CMD_TIC12400_SET_POLARITY:
            {
                uint32_t battery_switch_mask;

                if ((CAN_Protocol_DecodeTic12400PolarityCommand(
                         RxData, &battery_switch_mask) == 0U) ||
                    (TIC12400_Probe_SetBatterySwitchMask(
                         battery_switch_mask) == 0U))
                {
                    ack_flags &=
                        (uint8_t)~CAN_PROTOCOL_COMMAND_ACK_FLAG_EXECUTED;
                }
                break;
            }

            default:
                break;
        }

        CAN_Send_Command_Ack(
            command,
            CAN_PROTOCOL_COMMAND_ACK_ACCEPTED,
            ack_flags,
            frame_start_cycles);
    }

    if ((processed_count == CAN_APP_RX_FRAME_BUDGET_PER_PROCESS) &&
        (HAL_FDCAN_GetRxFifoFillLevel(
            &hfdcan1, FDCAN_RX_FIFO0) > 0U))
    {
        can_rx_stats.rx_budget_hits++;
    }
}

static void CAN_Apply_Slot_Policy(
    CAN_TxSlot_t *slot,
    uint8_t effective_running)
{
    uint8_t may_run =
        ((effective_running != 0U) &&
         (slot->requested_running != 0U) &&
         (slot->configured != 0U) &&
         (slot->active != 0U) &&
         (slot->counter_limit != 0U)) ? 1U : 0U;

    if ((may_run != 0U) && (slot->running == 0U))
    {
        slot->last_tx_time =
            HAL_GetTick() - slot->cycle_time_ms;
    }

    slot->running = may_run;
}

static void CAN_Apply_SlotPolicy(void)
{
    App_ControlSnapshot_t outputs = App_Control_GetSnapshot();
    App_ControlPolicySnapshot_t policy =
        App_ControlPolicy_GetSnapshot();
    uint8_t previous_slot_1 = tx_slot_1.running;
    uint8_t previous_slot_2 = tx_slot_2.running;
    uint8_t status_changed;

    CAN_Apply_Slot_Policy(
        &tx_slot_1,
        policy.slot_1_effective);
    CAN_Apply_Slot_Policy(
        &tx_slot_2,
        policy.slot_2_effective);

    status_changed =
        ((policy.update_count != control_policy_update_applied) ||
         (outputs.output_change_count != control_output_update_applied) ||
         (previous_slot_1 != tx_slot_1.running) ||
         (previous_slot_2 != tx_slot_2.running)) ? 1U : 0U;

    control_policy_update_applied = policy.update_count;
    control_output_update_applied = outputs.output_change_count;
    if (status_changed != 0U)
    {
        CAN_Send_System_Status();
        CAN_Send_Pwm_Status();
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

    App_ControlPolicy_SetLedRequest(led_no, led_state);
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
        CAN_Send_Rx_Health();
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
    CAN_Protocol_SystemStatus_t system_status = {0};
    App_ControlSnapshot_t outputs = App_Control_GetSnapshot();
    App_ControlPolicySnapshot_t policy =
        App_ControlPolicy_GetSnapshot();
    uint8_t txData[8] = {0};

    system_status.slot_1_running = tx_slot_1.running;
    system_status.slot_2_running = tx_slot_2.running;
    system_status.led_1_on = outputs.led_1_on;
    system_status.led_2_on = outputs.led_2_on;
    system_status.request_flags =
        ((policy.slot_1_requested != 0U)
            ? CAN_PROTOCOL_SYSTEM_REQUEST_SLOT_1 : 0U) |
        ((policy.slot_2_requested != 0U)
            ? CAN_PROTOCOL_SYSTEM_REQUEST_SLOT_2 : 0U) |
        ((policy.led_1_requested != 0U)
            ? CAN_PROTOCOL_SYSTEM_REQUEST_LED_1 : 0U) |
        ((policy.led_2_requested != 0U)
            ? CAN_PROTOCOL_SYSTEM_REQUEST_LED_2 : 0U) |
        ((policy.pwm_requested != 0U)
            ? CAN_PROTOCOL_SYSTEM_REQUEST_PWM : 0U);
    system_status.physical_flags =
        ((policy.switch_data_valid != 0U)
            ? CAN_PROTOCOL_SYSTEM_PHYSICAL_DATA_VALID : 0U) |
        (((policy.closed_switch_mask &
           (1UL << APP_CONTROL_POLICY_LED1_INPUT)) != 0U)
            ? CAN_PROTOCOL_SYSTEM_PHYSICAL_IN0_CLOSED : 0U) |
        (((policy.closed_switch_mask &
           (1UL << APP_CONTROL_POLICY_PWM_INPUT)) != 0U)
            ? CAN_PROTOCOL_SYSTEM_PHYSICAL_IN1_CLOSED : 0U) |
        (((policy.closed_switch_mask &
           (1UL << APP_CONTROL_POLICY_SLOT1_INPUT)) != 0U)
            ? CAN_PROTOCOL_SYSTEM_PHYSICAL_IN2_CLOSED : 0U) |
        (((policy.closed_switch_mask &
           (1UL << APP_CONTROL_POLICY_SLOT2_INPUT)) != 0U)
            ? CAN_PROTOCOL_SYSTEM_PHYSICAL_IN3_CLOSED : 0U);
    system_status.override_flags =
        ((policy.slot_1_blocked != 0U)
            ? CAN_PROTOCOL_SYSTEM_OVERRIDE_SLOT_1_BLOCKED : 0U) |
        ((policy.slot_2_blocked != 0U)
            ? CAN_PROTOCOL_SYSTEM_OVERRIDE_SLOT_2_BLOCKED : 0U) |
        ((policy.led_1_overridden != 0U)
            ? CAN_PROTOCOL_SYSTEM_OVERRIDE_LED_1_OVERRIDDEN : 0U) |
        ((policy.pwm_blocked != 0U)
            ? CAN_PROTOCOL_SYSTEM_OVERRIDE_PWM_BLOCKED : 0U);

    CAN_Protocol_EncodeSystemStatus(&system_status, txData);

    (void)CAN_Transport_SendClassicLatest(CAN_PROTOCOL_SYSTEM_STATUS_TX_ID,
                                          CAN_TRANSPORT_ID_STANDARD,
                                          txData);
}

static void CAN_Send_Rx_Health(void)
{
    CAN_App_RxStats_t stats;
    CAN_Protocol_CanRxHealth_t health = {0};
    uint8_t tx_data[CAN_PROTOCOL_PAYLOAD_SIZE] = {0};

    CAN_App_GetRxStats(&stats);

    health.flags =
        ((stats.rx_watermark_events != 0U)
            ? CAN_PROTOCOL_CAN_RX_HEALTH_WATERMARK_SEEN : 0U) |
        ((stats.rx_full_events != 0U)
            ? CAN_PROTOCOL_CAN_RX_HEALTH_FIFO_FULL_SEEN : 0U) |
        ((stats.rx_message_lost_events != 0U)
            ? CAN_PROTOCOL_CAN_RX_HEALTH_MESSAGE_LOST : 0U) |
        ((stats.rx_budget_hits != 0U)
            ? CAN_PROTOCOL_CAN_RX_HEALTH_BUDGET_EXHAUSTED : 0U) |
        ((stats.hal_rx_errors != 0U)
            ? CAN_PROTOCOL_CAN_RX_HEALTH_HAL_ERROR : 0U);
    health.max_fifo_fill =
        (stats.rx_max_fill_level > UINT8_MAX)
            ? UINT8_MAX
            : (uint8_t)stats.rx_max_fill_level;
    health.message_lost_events =
        CAN_SaturateU16(stats.rx_message_lost_events);
    health.fifo_full_events = CAN_SaturateU16(stats.rx_full_events);
    health.watermark_events =
        CAN_SaturateU16(stats.rx_watermark_events);

    CAN_Protocol_EncodeCanRxHealth(&health, tx_data);
    (void)CAN_Transport_SendClassicLatest(
        CAN_PROTOCOL_CAN_RX_HEALTH_TX_ID,
        CAN_TRANSPORT_ID_STANDARD,
        tx_data);
}

static void CAN_Timing_Telemetry_Process(void)
{
    App_Timing_Snapshot_t snapshot;
    App_Timing_AckSummary_t ack_summary;
    CAN_Protocol_TimingService_t service_timing;
    CAN_Protocol_TimingAckLatency_t ack_timing;
    App_Timing_ServiceStats_t *service_stats;
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE] = {0};
    uint32_t now = HAL_GetTick();

    if ((int32_t)(now - timing_service_next_tick) >= 0)
    {
        do
        {
            timing_service_next_tick += CAN_TIMING_SERVICE_PERIOD_MS;
        }
        while ((int32_t)(now - timing_service_next_tick) >= 0);

        App_Timing_GetSnapshot(&snapshot);
        service_stats = &snapshot.service[timing_service_index];
        service_timing.service_id = timing_service_index;
        service_timing.flags =
            ((snapshot.enabled != 0U)
                ? CAN_PROTOCOL_TIMING_SERVICE_ENABLED : 0U) |
            ((service_stats->current_cycles >
              service_stats->budget_cycles)
                ? CAN_PROTOCOL_TIMING_SERVICE_CURRENT_OVERRUN : 0U) |
            ((service_stats->overrun_count != 0U)
                ? CAN_PROTOCOL_TIMING_SERVICE_OVERRUN_LATCHED : 0U);
        service_timing.current_us = CAN_SaturateU16(
            App_Timing_CyclesToMicroseconds(
                service_stats->current_cycles));
        service_timing.minimum_us = CAN_SaturateU16(
            App_Timing_CyclesToMicroseconds(
                service_stats->minimum_cycles));
        service_timing.maximum_us = CAN_SaturateU16(
            App_Timing_CyclesToMicroseconds(
                service_stats->maximum_cycles));
        CAN_Protocol_EncodeTimingService(&service_timing, payload);
        (void)CAN_Transport_SendClassicLatest(
            CAN_PROTOCOL_TIMING_SERVICE_TX_ID,
            CAN_TRANSPORT_ID_STANDARD,
            payload);

        timing_service_index++;
        if (timing_service_index >= (uint8_t)APP_TIMING_SERVICE_COUNT)
        {
            timing_service_index = 0U;
        }
    }

    if ((int32_t)(now - timing_ack_next_tick) < 0)
    {
        return;
    }

    do
    {
        timing_ack_next_tick += CAN_TIMING_ACK_PERIOD_MS;
    }
    while ((int32_t)(now - timing_ack_next_tick) >= 0);

    App_Timing_GetAckSummary(&ack_summary);
    ack_timing.p50_us = CAN_SaturateU16(ack_summary.p50_us);
    ack_timing.p95_us = CAN_SaturateU16(ack_summary.p95_us);
    ack_timing.p99_us = CAN_SaturateU16(ack_summary.p99_us);
    ack_timing.maximum_us = CAN_SaturateU16(ack_summary.maximum_us);
    CAN_Protocol_EncodeTimingAckLatency(&ack_timing, payload);
    (void)CAN_Transport_SendClassicLatest(
        CAN_PROTOCOL_TIMING_ACK_LATENCY_TX_ID,
        CAN_TRANSPORT_ID_STANDARD,
        payload);
}

void CAN_Send_Pwm_Status(void)
{
    CAN_Protocol_PwmStatus_t pwm_status = {0};
    App_ControlPolicySnapshot_t policy =
        App_ControlPolicy_GetSnapshot();
    uint8_t txData[8] = {0};
    PWM_Control_State_t state = PWM_Control_GetState();

    pwm_status.running = state.running;
    pwm_status.duty_percent = state.duty_percent;
    pwm_status.actual_frequency_hz = state.actual_frequency_hz;
    pwm_status.control_flags =
        ((policy.pwm_requested != 0U)
            ? CAN_PROTOCOL_PWM_CONTROL_REQUESTED : 0U) |
        ((policy.pwm_permitted != 0U)
            ? CAN_PROTOCOL_PWM_CONTROL_PHYSICAL_PERMITTED : 0U) |
        ((policy.pwm_blocked != 0U)
            ? CAN_PROTOCOL_PWM_CONTROL_BLOCKED : 0U) |
        ((policy.switch_data_valid != 0U)
            ? CAN_PROTOCOL_PWM_CONTROL_SWITCH_DATA_VALID : 0U);

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
        CAN_App_RequestControlAccessFromIsr();
    }
}
