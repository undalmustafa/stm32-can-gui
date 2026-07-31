#include "unity.h"

#include "app_control.h"
#include "app_control_policy.h"
#include "app_diagnostics.h"
#include "app_log.h"
#include "app_log_can.h"
#include "app_timing.h"
#include "can_app.h"
#include "can_app_init.h"
#include "can_control_access.h"
#include "can_protocol.h"
#include "can_recovery.h"
#include "can_rx_health.h"
#include "can_transport.h"
#include "pca2131.h"
#include "pwm_control.h"
#include "pwm_self_test.h"
#include "rtc_app.h"
#include "tic12400_probe.h"

#include <string.h>

#define TEST_FRAME_CAPACITY 16U
#define TEST_ACK_CAPACITY   16U
#define TEST_LOG_CAPACITY   16U

typedef struct
{
    FDCAN_RxHeaderTypeDef header;
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE];
} Test_RxFrame_t;

typedef struct
{
    uint32_t identifier;
    CAN_Transport_IdType_t id_type;
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE];
} Test_Ack_t;

typedef struct
{
    App_Log_Severity_t severity;
    App_Log_EventCode_t event;
    uint32_t data_0;
    uint32_t data_1;
} Test_Log_t;

FDCAN_GlobalTypeDef test_fdcan1_instance;
FDCAN_HandleTypeDef hfdcan1;
volatile CAN_App_State_t g_canAppState;

static Test_RxFrame_t frames[TEST_FRAME_CAPACITY];
static uint32_t frame_count;
static uint32_t frame_index;
static HAL_StatusTypeDef read_status;
static uint32_t read_calls;
static uint32_t fake_tick;
static CAN_RxHealth_Stats_t health_stats;
static Test_Ack_t acks[TEST_ACK_CAPACITY];
static uint32_t ack_count;
static CAN_Transport_Result_t ack_result;
static Test_Log_t logs[TEST_LOG_CAPACITY];
static uint32_t log_count;
static uint32_t led_request_calls;
static uint8_t last_led_number;
static uint8_t last_led_state;
static uint32_t log_info_calls;
static uint32_t timing_ack_records;

uint32_t HAL_GetTick(void)
{
    return fake_tick;
}

uint32_t HAL_FDCAN_GetRxFifoFillLevel(FDCAN_HandleTypeDef *hfdcan,
                                      uint32_t RxFifo)
{
    TEST_ASSERT_EQUAL_PTR(&hfdcan1, hfdcan);
    TEST_ASSERT_EQUAL_UINT32(FDCAN_RX_FIFO0, RxFifo);
    return frame_count - frame_index;
}

HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(
    FDCAN_HandleTypeDef *hfdcan,
    uint32_t RxLocation,
    FDCAN_RxHeaderTypeDef *header,
    uint8_t *payload)
{
    TEST_ASSERT_EQUAL_PTR(&hfdcan1, hfdcan);
    TEST_ASSERT_EQUAL_UINT32(FDCAN_RX_FIFO0, RxLocation);
    read_calls++;

    if (read_status != HAL_OK)
    {
        return read_status;
    }

    *header = frames[frame_index].header;
    (void)memcpy(payload,
                 frames[frame_index].payload,
                 CAN_PROTOCOL_PAYLOAD_SIZE);
    frame_index++;
    return HAL_OK;
}

CAN_App_InitResult_t CAN_App_InitHardware(FDCAN_HandleTypeDef *hfdcan,
                                          uint8_t peripheral_ready)
{
    TEST_ASSERT_EQUAL_PTR(&hfdcan1, hfdcan);
    g_canAppState = (CAN_App_State_t){0};
    g_canAppState.init_attempts = 1U;
    g_canAppState.available = peripheral_ready;
    g_canAppState.init_result = (peripheral_ready != 0U)
        ? CAN_APP_INIT_OK
        : CAN_APP_INIT_PERIPHERAL_UNAVAILABLE;
    return g_canAppState.init_result;
}

void CAN_RxHealth_Init(void)
{
    health_stats = (CAN_RxHealth_Stats_t){0};
}

void CAN_RxHealth_GetStats(CAN_RxHealth_Stats_t *stats)
{
    *stats = health_stats;
}

void CAN_Recovery_Init(void)
{
}

void App_Log_Can_Init(void)
{
}

void App_Log_Can_SendInfo(void)
{
    log_info_calls++;
}

void App_Log_Can_SendRecord(uint32_t sequence)
{
    (void)sequence;
}

void App_Diagnostics_Init(void)
{
}

uint8_t App_Log_Push(App_Log_Source_t source,
                     App_Log_Severity_t severity,
                     App_Log_EventCode_t event_code,
                     uint32_t data_0,
                     uint32_t data_1)
{
    TEST_ASSERT_EQUAL(APP_LOG_SOURCE_CAN, source);
    if (log_count < TEST_LOG_CAPACITY)
    {
        logs[log_count].severity = severity;
        logs[log_count].event = event_code;
        logs[log_count].data_0 = data_0;
        logs[log_count].data_1 = data_1;
        log_count++;
        return 1U;
    }
    return 0U;
}

CAN_Transport_Result_t CAN_Transport_SendClassicHighPriority(
    uint32_t identifier,
    CAN_Transport_IdType_t id_type,
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    if (ack_count < TEST_ACK_CAPACITY)
    {
        acks[ack_count].identifier = identifier;
        acks[ack_count].id_type = id_type;
        (void)memcpy(acks[ack_count].payload,
                     payload,
                     CAN_PROTOCOL_PAYLOAD_SIZE);
        ack_count++;
    }
    return ack_result;
}

CAN_Transport_Result_t CAN_Transport_SendClassicLatest(
    uint32_t identifier,
    CAN_Transport_IdType_t id_type,
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    (void)identifier;
    (void)id_type;
    (void)payload;
    return CAN_TRANSPORT_OK;
}

uint32_t App_Timing_Begin(void)
{
    return 100U;
}

uint32_t App_Timing_Now(void)
{
    return 110U;
}

void App_Timing_RecordAckElapsed(uint32_t start_cycles,
                                 uint32_t end_cycles)
{
    TEST_ASSERT_EQUAL_UINT32(100U, start_cycles);
    TEST_ASSERT_EQUAL_UINT32(110U, end_cycles);
    timing_ack_records++;
}

void App_ControlPolicy_SetLedRequest(uint8_t led_number,
                                    uint8_t requested_on)
{
    led_request_calls++;
    last_led_number = led_number;
    last_led_state = requested_on;
}

void App_ControlPolicy_SetPwmRequest(uint8_t requested_on)
{
    (void)requested_on;
}

void App_ControlPolicy_SetSlotRequest(uint8_t slot_number,
                                     uint8_t requested_on)
{
    (void)slot_number;
    (void)requested_on;
}

App_ControlPolicySnapshot_t App_ControlPolicy_GetSnapshot(void)
{
    App_ControlPolicySnapshot_t snapshot = {0};
    return snapshot;
}

void CAN_Handle_RTC_Set_Time(uint8_t *data)
{
    (void)data;
}

void CAN_Handle_RTC_Set_DateTime(uint8_t *data)
{
    (void)data;
}

void CAN_Handle_RTC_Set_Alarm(uint8_t *data)
{
    (void)data;
}

PWM_Control_Result_t PWM_Control_Set(uint32_t frequency_hz,
                                     uint8_t duty_percent)
{
    (void)frequency_hz;
    (void)duty_percent;
    return PWM_CONTROL_OK;
}

uint8_t PWM_SelfTest_IsRunning(void)
{
    return 0U;
}

void PWM_SelfTest_Cancel(void)
{
}

PWM_SelfTest_ResultCode_t PWM_SelfTest_Start(void)
{
    return PWM_SELF_TEST_OK;
}

PWM_SelfTest_State_t PWM_SelfTest_GetState(void)
{
    PWM_SelfTest_State_t state = {0};
    return state;
}

uint8_t TIC12400_Probe_SetBatterySwitchMask(uint32_t battery_switch_mask)
{
    (void)battery_switch_mask;
    return 1U;
}

uint8_t PCA2131_Driver_IsValidDateTime(
    const PCA2131_DateTime_t *date_time)
{
    return (date_time != NULL) ? 1U : 0U;
}

static void enqueue_frame(const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    Test_RxFrame_t *frame = &frames[frame_count];

    frame->header.Identifier = CAN_PROTOCOL_GUI_COMMAND_ID_EXT;
    frame->header.IdType = FDCAN_EXTENDED_ID;
    frame->header.RxFrameType = FDCAN_DATA_FRAME;
    frame->header.DataLength = FDCAN_DLC_BYTES_8;
    frame->header.FDFormat = FDCAN_CLASSIC_CAN;
    (void)memcpy(frame->payload, payload, CAN_PROTOCOL_PAYLOAD_SIZE);
    frame_count++;
}

static void reset_captures(void)
{
    frame_count = 0U;
    frame_index = 0U;
    read_status = HAL_OK;
    read_calls = 0U;
    fake_tick = 1000U;
    ack_count = 0U;
    ack_result = CAN_TRANSPORT_OK;
    log_count = 0U;
    led_request_calls = 0U;
    last_led_number = 0U;
    last_led_state = 0U;
    log_info_calls = 0U;
    timing_ack_records = 0U;
    (void)memset(frames, 0, sizeof(frames));
    (void)memset(acks, 0, sizeof(acks));
    (void)memset(logs, 0, sizeof(logs));
}

void setUp(void)
{
    test_fdcan1_instance = (FDCAN_GlobalTypeDef){0};
    hfdcan1 = (FDCAN_HandleTypeDef){0};
    hfdcan1.Instance = FDCAN1;
    hfdcan1.ErrorCode = 0x1234U;
    reset_captures();
    TEST_ASSERT_EQUAL(CAN_APP_INIT_OK, CAN_App_Init(1U));
    reset_captures();
}

void tearDown(void)
{
}

static CAN_App_RxStats_t get_stats(void)
{
    CAN_App_RxStats_t stats;
    CAN_App_GetRxStats(&stats);
    return stats;
}

void test_wrong_id_is_rejected_without_ack(void)
{
    const uint8_t command[8] =
        {CAN_PROTOCOL_CMD_LOG_GET_INFO, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    CAN_App_RxStats_t stats;

    enqueue_frame(command);
    frames[0].header.Identifier++;
    CAN_Process_Rx_Command();
    stats = get_stats();

    TEST_ASSERT_EQUAL_UINT32(1U, stats.frames_received);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.rejected_wrong_id);
    TEST_ASSERT_EQUAL_UINT32(0U, ack_count);
    TEST_ASSERT_EQUAL(APP_LOG_EVENT_CAN_RX_REJECTED, logs[0].event);
    TEST_ASSERT_EQUAL_UINT32(CAN_APP_RX_REJECT_WRONG_ID, logs[0].data_0);
}

void test_frame_format_and_dlc_gates_are_counted_without_ack(void)
{
    const uint8_t command[8] =
        {CAN_PROTOCOL_CMD_LOG_GET_INFO, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    CAN_App_RxStats_t stats;

    enqueue_frame(command);
    frames[0].header.RxFrameType = FDCAN_REMOTE_FRAME;
    enqueue_frame(command);
    frames[1].header.FDFormat = FDCAN_FD_CAN;
    enqueue_frame(command);
    frames[2].header.DataLength = 4U;

    CAN_Process_Rx_Command();
    stats = get_stats();
    TEST_ASSERT_EQUAL_UINT32(3U, stats.frames_received);
    TEST_ASSERT_EQUAL_UINT32(2U, stats.rejected_frame_format);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.rejected_dlc);
    TEST_ASSERT_EQUAL_UINT32(0U, ack_count);
}

void test_unknown_and_invalid_commands_return_reasoned_acks(void)
{
    const uint8_t unknown[8] =
        {0xFFU, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t invalid_led[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 0U, 1U, 0U, 0U, 0U, 0U, 0U};
    CAN_App_RxStats_t stats;

    enqueue_frame(unknown);
    enqueue_frame(invalid_led);
    CAN_Process_Rx_Command();
    stats = get_stats();

    TEST_ASSERT_EQUAL_UINT32(1U, stats.rejected_unknown_command);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.rejected_invalid_payload);
    TEST_ASSERT_EQUAL_UINT32(2U, stats.command_acks_sent);
    TEST_ASSERT_EQUAL_UINT32(2U, timing_ack_records);
    TEST_ASSERT_EQUAL_UINT32(2U, ack_count);
    TEST_ASSERT_EQUAL_HEX32(CAN_PROTOCOL_COMMAND_ACK_TX_ID,
                            acks[0].identifier);
    TEST_ASSERT_EQUAL(CAN_TRANSPORT_ID_STANDARD, acks[0].id_type);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_VERSION, acks[0].payload[0]);
    TEST_ASSERT_EQUAL_UINT8(unknown[0], acks[0].payload[1]);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_COMMAND_ACK_UNKNOWN_COMMAND,
                            acks[0].payload[3]);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_COMMAND_ACK_INVALID_PAYLOAD,
                            acks[1].payload[3]);
    TEST_ASSERT_EQUAL_HEX8(CAN_Protocol_CalculateCommandToken(unknown),
                           acks[0].payload[2]);
}

void test_privileged_command_is_denied_while_safe_command_executes(void)
{
    const uint8_t led_on[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 1U, 1U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t led_off[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 1U, 0U, 0U, 0U, 0U, 0U, 0U};
    CAN_App_RxStats_t stats;

    enqueue_frame(led_on);
    enqueue_frame(led_off);
    CAN_Process_Rx_Command();
    stats = get_stats();

    TEST_ASSERT_EQUAL_UINT32(1U, stats.rejected_access_denied);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.commands_accepted);
    TEST_ASSERT_EQUAL_UINT32(1U, led_request_calls);
    TEST_ASSERT_EQUAL_UINT8(1U, last_led_number);
    TEST_ASSERT_EQUAL_UINT8(0U, last_led_state);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_COMMAND_ACK_ACCESS_DENIED,
                            acks[0].payload[3]);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_COMMAND_ACK_ACCEPTED,
                            acks[1].payload[3]);
    TEST_ASSERT_BITS(CAN_PROTOCOL_COMMAND_ACK_FLAG_EXECUTED,
                     CAN_PROTOCOL_COMMAND_ACK_FLAG_EXECUTED,
                     acks[1].payload[4]);
}

void test_button_request_opens_window_before_privileged_command(void)
{
    const uint8_t led_on[8] =
        {CAN_PROTOCOL_CMD_LED_CONTROL, 2U, 1U, 0U, 0U, 0U, 0U, 0U};
    CAN_App_RxStats_t stats;

    CAN_App_RequestControlAccess();
    enqueue_frame(led_on);
    CAN_Process_Rx_Command();
    stats = get_stats();

    TEST_ASSERT_EQUAL_UINT32(1U, stats.commands_accepted);
    TEST_ASSERT_EQUAL_UINT32(0U, stats.rejected_access_denied);
    TEST_ASSERT_EQUAL_UINT32(1U, led_request_calls);
    TEST_ASSERT_EQUAL(APP_LOG_EVENT_CAN_CONTROL_ACCESS_OPENED, logs[0].event);
    TEST_ASSERT_EQUAL_UINT32(CAN_CONTROL_ACCESS_WINDOW_MS, logs[0].data_0);
    TEST_ASSERT_BITS(CAN_PROTOCOL_COMMAND_ACK_FLAG_ACCESS_OPEN,
                     CAN_PROTOCOL_COMMAND_ACK_FLAG_ACCESS_OPEN,
                     acks[0].payload[4]);
    TEST_ASSERT_EQUAL_UINT8(240U, acks[0].payload[5]);
}

void test_hal_read_failure_breaks_loop_and_records_fault(void)
{
    const uint8_t command[8] =
        {CAN_PROTOCOL_CMD_LOG_GET_INFO, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    CAN_App_RxStats_t stats;

    enqueue_frame(command);
    read_status = HAL_ERROR;
    CAN_Process_Rx_Command();
    stats = get_stats();

    TEST_ASSERT_EQUAL_UINT32(1U, read_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, stats.frames_received);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.hal_rx_errors);
    TEST_ASSERT_EQUAL_UINT32(0U, ack_count);
    TEST_ASSERT_EQUAL(APP_LOG_SEVERITY_FAULT, logs[0].severity);
    TEST_ASSERT_EQUAL_HEX32(hfdcan1.ErrorCode, logs[0].data_1);
}

void test_process_budget_leaves_ninth_frame_for_next_pass(void)
{
    const uint8_t command[8] =
        {CAN_PROTOCOL_CMD_LOG_GET_INFO, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    CAN_App_RxStats_t stats;
    uint32_t index;

    for (index = 0U; index < 9U; index++)
    {
        enqueue_frame(command);
    }

    CAN_Process_Rx_Command();
    stats = get_stats();
    TEST_ASSERT_EQUAL_UINT32(8U, stats.frames_received);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.rx_budget_hits);
    TEST_ASSERT_EQUAL_UINT32(8U, ack_count);
    TEST_ASSERT_EQUAL_UINT32(8U, log_info_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, frame_count - frame_index);

    CAN_Process_Rx_Command();
    stats = get_stats();
    TEST_ASSERT_EQUAL_UINT32(9U, stats.frames_received);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.rx_budget_hits);
    TEST_ASSERT_EQUAL_UINT32(9U, ack_count);
}

void test_ack_transport_failure_is_visible_in_stats(void)
{
    const uint8_t command[8] =
        {CAN_PROTOCOL_CMD_LOG_GET_INFO, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    CAN_App_RxStats_t stats;

    ack_result = CAN_TRANSPORT_QUEUE_FULL;
    enqueue_frame(command);
    CAN_Process_Rx_Command();
    stats = get_stats();

    TEST_ASSERT_EQUAL_UINT32(0U, stats.command_acks_sent);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.command_ack_tx_failures);
    TEST_ASSERT_EQUAL_UINT32(1U, ack_count);
}

void test_rx_stats_include_isr_health_snapshot(void)
{
    CAN_App_RxStats_t stats;

    health_stats.new_message_events = 10U;
    health_stats.watermark_events = 2U;
    health_stats.full_events = 3U;
    health_stats.message_lost_events = 4U;
    health_stats.max_fill_level = 31U;
    stats = get_stats();

    TEST_ASSERT_EQUAL_UINT32(10U, stats.rx_new_message_events);
    TEST_ASSERT_EQUAL_UINT32(2U, stats.rx_watermark_events);
    TEST_ASSERT_EQUAL_UINT32(3U, stats.rx_full_events);
    TEST_ASSERT_EQUAL_UINT32(4U, stats.rx_message_lost_events);
    TEST_ASSERT_EQUAL_UINT32(31U, stats.rx_max_fill_level);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_wrong_id_is_rejected_without_ack);
    RUN_TEST(test_frame_format_and_dlc_gates_are_counted_without_ack);
    RUN_TEST(test_unknown_and_invalid_commands_return_reasoned_acks);
    RUN_TEST(
        test_privileged_command_is_denied_while_safe_command_executes);
    RUN_TEST(test_button_request_opens_window_before_privileged_command);
    RUN_TEST(test_hal_read_failure_breaks_loop_and_records_fault);
    RUN_TEST(test_process_budget_leaves_ninth_frame_for_next_pass);
    RUN_TEST(test_ack_transport_failure_is_visible_in_stats);
    RUN_TEST(test_rx_stats_include_isr_health_snapshot);
    return UNITY_END();
}
