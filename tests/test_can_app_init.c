#include "unity.h"

#include "app_log.h"
#include "can_app_init.h"
#include "can_protocol_generated.h"
#include "can_recovery.h"
#include "can_transport.h"

#define TEST_TRANSPORT_CALL_CAPACITY 4U

typedef enum
{
    TEST_INIT_STAGE_NONE = 0U,
    TEST_INIT_STAGE_FILTER,
    TEST_INIT_STAGE_GLOBAL_FILTER,
    TEST_INIT_STAGE_FIFO_MODE,
    TEST_INIT_STAGE_WATERMARK,
    TEST_INIT_STAGE_START,
    TEST_INIT_STAGE_NOTIFICATION
} Test_InitStage_t;

typedef struct
{
    uint32_t filter;
    uint32_t global_filter;
    uint32_t fifo_mode;
    uint32_t watermark;
    uint32_t start;
    uint32_t notification;
    uint32_t stop;
} Test_CallCounts_t;

FDCAN_GlobalTypeDef test_fdcan1_instance;

static FDCAN_HandleTypeDef test_handle;
static Test_InitStage_t failing_stage;
static Test_CallCounts_t calls;
static FDCAN_FilterTypeDef captured_filter;
static uint32_t captured_global_args[4];
static uint32_t captured_fifo_args[2];
static uint32_t captured_watermark_args[2];
static FDCAN_HandleTypeDef *transport_bindings[TEST_TRANSPORT_CALL_CAPACITY];
static uint32_t transport_init_calls;
static App_Log_Source_t logged_source;
static App_Log_Severity_t logged_severity;
static App_Log_EventCode_t logged_event;
static uint32_t logged_data_0;
static uint32_t logged_data_1;
static uint32_t log_calls;
static uint32_t recovery_init_calls;

static HAL_StatusTypeDef status_for(Test_InitStage_t stage)
{
    return (failing_stage == stage) ? HAL_ERROR : HAL_OK;
}

void CAN_Transport_Init(FDCAN_HandleTypeDef *hfdcan)
{
    if (transport_init_calls < TEST_TRANSPORT_CALL_CAPACITY)
    {
        transport_bindings[transport_init_calls] = hfdcan;
    }
    transport_init_calls++;
}

uint8_t App_Log_Push(App_Log_Source_t source,
                     App_Log_Severity_t severity,
                     App_Log_EventCode_t event_code,
                     uint32_t data_0,
                     uint32_t data_1)
{
    logged_source = source;
    logged_severity = severity;
    logged_event = event_code;
    logged_data_0 = data_0;
    logged_data_1 = data_1;
    log_calls++;
    return 1U;
}

HAL_StatusTypeDef HAL_FDCAN_ConfigFilter(
    FDCAN_HandleTypeDef *hfdcan,
    FDCAN_FilterTypeDef *filter)
{
    TEST_ASSERT_EQUAL_PTR(&test_handle, hfdcan);
    calls.filter++;
    captured_filter = *filter;
    return status_for(TEST_INIT_STAGE_FILTER);
}

HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(
    FDCAN_HandleTypeDef *hfdcan,
    uint32_t NonMatchingStd,
    uint32_t NonMatchingExt,
    uint32_t RejectRemoteStd,
    uint32_t RejectRemoteExt)
{
    TEST_ASSERT_EQUAL_PTR(&test_handle, hfdcan);
    calls.global_filter++;
    captured_global_args[0] = NonMatchingStd;
    captured_global_args[1] = NonMatchingExt;
    captured_global_args[2] = RejectRemoteStd;
    captured_global_args[3] = RejectRemoteExt;
    return status_for(TEST_INIT_STAGE_GLOBAL_FILTER);
}

HAL_StatusTypeDef HAL_FDCAN_ConfigRxFifoOverwrite(
    FDCAN_HandleTypeDef *hfdcan,
    uint32_t RxFifo,
    uint32_t OperationMode)
{
    TEST_ASSERT_EQUAL_PTR(&test_handle, hfdcan);
    calls.fifo_mode++;
    captured_fifo_args[0] = RxFifo;
    captured_fifo_args[1] = OperationMode;
    return status_for(TEST_INIT_STAGE_FIFO_MODE);
}

HAL_StatusTypeDef HAL_FDCAN_ConfigFifoWatermark(
    FDCAN_HandleTypeDef *hfdcan,
    uint32_t Fifo,
    uint32_t Watermark)
{
    TEST_ASSERT_EQUAL_PTR(&test_handle, hfdcan);
    calls.watermark++;
    captured_watermark_args[0] = Fifo;
    captured_watermark_args[1] = Watermark;
    return status_for(TEST_INIT_STAGE_WATERMARK);
}

HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *hfdcan)
{
    TEST_ASSERT_EQUAL_PTR(&test_handle, hfdcan);
    calls.start++;
    return status_for(TEST_INIT_STAGE_START);
}

HAL_StatusTypeDef CAN_Recovery_EnableNotifications(
    FDCAN_HandleTypeDef *hfdcan)
{
    TEST_ASSERT_EQUAL_PTR(&test_handle, hfdcan);
    calls.notification++;
    return status_for(TEST_INIT_STAGE_NOTIFICATION);
}

void CAN_Recovery_Init(void)
{
    recovery_init_calls++;
}

HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef *hfdcan)
{
    TEST_ASSERT_EQUAL_PTR(&test_handle, hfdcan);
    calls.stop++;
    return HAL_OK;
}

static void reset_fakes(void)
{
    uint32_t index;

    test_fdcan1_instance = (FDCAN_GlobalTypeDef){0};
    test_handle = (FDCAN_HandleTypeDef){0};
    test_handle.Instance = FDCAN1;
    test_handle.ErrorCode = 0xA5A55A5AUL;
    failing_stage = TEST_INIT_STAGE_NONE;
    calls = (Test_CallCounts_t){0};
    captured_filter = (FDCAN_FilterTypeDef){0};
    for (index = 0U; index < 4U; index++)
    {
        captured_global_args[index] = 0U;
        transport_bindings[index] = NULL;
    }
    captured_fifo_args[0] = 0U;
    captured_fifo_args[1] = 0U;
    captured_watermark_args[0] = 0U;
    captured_watermark_args[1] = 0U;
    transport_init_calls = 0U;
    logged_source = APP_LOG_SOURCE_SYSTEM;
    logged_severity = APP_LOG_SEVERITY_INFO;
    logged_event = APP_LOG_EVENT_SYSTEM_BOOT;
    logged_data_0 = 0U;
    logged_data_1 = 0U;
    log_calls = 0U;
    recovery_init_calls = 0U;
}

void setUp(void)
{
    reset_fakes();
}

void tearDown(void)
{
}

static void assert_failed_init(CAN_App_InitResult_t expected_result,
                               Test_InitStage_t stage)
{
    CAN_App_State_t state;

    failing_stage = stage;
    TEST_ASSERT_EQUAL(expected_result,
                      CAN_App_InitHardware(&test_handle, 1U));
    state = CAN_App_GetState();

    TEST_ASSERT_EQUAL_UINT32(1U, state.init_attempts);
    TEST_ASSERT_EQUAL_UINT32(1U, recovery_init_calls);
    TEST_ASSERT_EQUAL_UINT8(0U, state.available);
    TEST_ASSERT_EQUAL(expected_result, state.init_result);
    TEST_ASSERT_EQUAL_HEX32(test_handle.ErrorCode, state.hal_error);
    TEST_ASSERT_EQUAL_UINT32(3U, transport_init_calls);
    TEST_ASSERT_NULL(transport_bindings[0]);
    TEST_ASSERT_EQUAL_PTR(&test_handle, transport_bindings[1]);
    TEST_ASSERT_NULL(transport_bindings[2]);
    TEST_ASSERT_EQUAL_UINT32(1U, log_calls);
    TEST_ASSERT_EQUAL(APP_LOG_SOURCE_CAN, logged_source);
    TEST_ASSERT_EQUAL(APP_LOG_SEVERITY_FAULT, logged_severity);
    TEST_ASSERT_EQUAL(APP_LOG_EVENT_CAN_STARTUP_FAILED, logged_event);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)expected_result, logged_data_0);
    TEST_ASSERT_EQUAL_HEX32(test_handle.ErrorCode, logged_data_1);
}

void test_success_configures_filters_fifo_and_enables_transport(void)
{
    CAN_App_State_t state;

    TEST_ASSERT_EQUAL(CAN_APP_INIT_OK,
                      CAN_App_InitHardware(&test_handle, 1U));
    state = CAN_App_GetState();

    TEST_ASSERT_EQUAL_UINT32(1U, state.init_attempts);
    TEST_ASSERT_EQUAL_UINT32(1U, recovery_init_calls);
    TEST_ASSERT_EQUAL_UINT8(1U, state.available);
    TEST_ASSERT_EQUAL(CAN_APP_INIT_OK, state.init_result);
    TEST_ASSERT_EQUAL_UINT32(0U, state.hal_error);
    TEST_ASSERT_EQUAL_UINT8(1U, CAN_App_IsAvailable());
    TEST_ASSERT_EQUAL_UINT32(2U, transport_init_calls);
    TEST_ASSERT_NULL(transport_bindings[0]);
    TEST_ASSERT_EQUAL_PTR(&test_handle, transport_bindings[1]);
    TEST_ASSERT_EQUAL_UINT32(0U, log_calls);

    TEST_ASSERT_EQUAL_UINT32(1U, calls.filter);
    TEST_ASSERT_EQUAL_UINT32(FDCAN_EXTENDED_ID, captured_filter.IdType);
    TEST_ASSERT_EQUAL_UINT32(FDCAN_FILTER_MASK,
                             captured_filter.FilterType);
    TEST_ASSERT_EQUAL_UINT32(FDCAN_FILTER_TO_RXFIFO0,
                             captured_filter.FilterConfig);
    TEST_ASSERT_EQUAL_HEX32(CAN_PROTOCOL_GUI_COMMAND_ID_EXT,
                            captured_filter.FilterID1);
    TEST_ASSERT_EQUAL_HEX32(0x1FFFFFFFUL, captured_filter.FilterID2);
    TEST_ASSERT_EQUAL_UINT32(FDCAN_REJECT, captured_global_args[0]);
    TEST_ASSERT_EQUAL_UINT32(FDCAN_REJECT, captured_global_args[1]);
    TEST_ASSERT_EQUAL_UINT32(FDCAN_REJECT_REMOTE,
                             captured_global_args[2]);
    TEST_ASSERT_EQUAL_UINT32(FDCAN_REJECT_REMOTE,
                             captured_global_args[3]);
    TEST_ASSERT_EQUAL_UINT32(FDCAN_RX_FIFO0, captured_fifo_args[0]);
    TEST_ASSERT_EQUAL_UINT32(FDCAN_RX_FIFO_BLOCKING,
                             captured_fifo_args[1]);
    TEST_ASSERT_EQUAL_UINT32(FDCAN_CFG_RX_FIFO0,
                             captured_watermark_args[0]);
    TEST_ASSERT_EQUAL_UINT32(CAN_APP_RX_FIFO0_WATERMARK,
                             captured_watermark_args[1]);
    TEST_ASSERT_EQUAL_UINT32(1U, calls.start);
    TEST_ASSERT_EQUAL_UINT32(1U, calls.notification);
    TEST_ASSERT_EQUAL_UINT32(0U, calls.stop);
}

void test_unavailable_or_null_peripheral_never_touches_hal(void)
{
    CAN_App_State_t state;

    TEST_ASSERT_EQUAL(CAN_APP_INIT_PERIPHERAL_UNAVAILABLE,
                      CAN_App_InitHardware(&test_handle, 0U));
    state = CAN_App_GetState();
    TEST_ASSERT_EQUAL_UINT8(0U, state.available);
    TEST_ASSERT_EQUAL_UINT32(1U, recovery_init_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, calls.filter);
    TEST_ASSERT_EQUAL_UINT32(2U, transport_init_calls);
    TEST_ASSERT_NULL(transport_bindings[0]);
    TEST_ASSERT_NULL(transport_bindings[1]);

    reset_fakes();
    TEST_ASSERT_EQUAL(CAN_APP_INIT_PERIPHERAL_UNAVAILABLE,
                      CAN_App_InitHardware(NULL, 1U));
    state = CAN_App_GetState();
    TEST_ASSERT_EQUAL_UINT32(0U, state.hal_error);
    TEST_ASSERT_EQUAL_UINT32(1U, recovery_init_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, calls.filter);
}

void test_filter_failure_disables_transport_and_stops_sequence(void)
{
    assert_failed_init(CAN_APP_INIT_FILTER_ERROR,
                       TEST_INIT_STAGE_FILTER);
    TEST_ASSERT_EQUAL_UINT32(1U, calls.filter);
    TEST_ASSERT_EQUAL_UINT32(0U, calls.global_filter);
}

void test_global_filter_failure_stops_before_fifo_configuration(void)
{
    assert_failed_init(CAN_APP_INIT_GLOBAL_FILTER_ERROR,
                       TEST_INIT_STAGE_GLOBAL_FILTER);
    TEST_ASSERT_EQUAL_UINT32(1U, calls.filter);
    TEST_ASSERT_EQUAL_UINT32(1U, calls.global_filter);
    TEST_ASSERT_EQUAL_UINT32(0U, calls.fifo_mode);
}

void test_fifo_mode_failure_stops_before_watermark(void)
{
    assert_failed_init(CAN_APP_INIT_FIFO_MODE_ERROR,
                       TEST_INIT_STAGE_FIFO_MODE);
    TEST_ASSERT_EQUAL_UINT32(1U, calls.fifo_mode);
    TEST_ASSERT_EQUAL_UINT32(0U, calls.watermark);
}

void test_watermark_failure_stops_before_start(void)
{
    assert_failed_init(CAN_APP_INIT_FIFO_WATERMARK_ERROR,
                       TEST_INIT_STAGE_WATERMARK);
    TEST_ASSERT_EQUAL_UINT32(1U, calls.watermark);
    TEST_ASSERT_EQUAL_UINT32(0U, calls.start);
}

void test_start_failure_does_not_enable_notifications(void)
{
    assert_failed_init(CAN_APP_INIT_START_ERROR,
                       TEST_INIT_STAGE_START);
    TEST_ASSERT_EQUAL_UINT32(1U, calls.start);
    TEST_ASSERT_EQUAL_UINT32(0U, calls.notification);
    TEST_ASSERT_EQUAL_UINT32(0U, calls.stop);
}

void test_notification_failure_stops_controller_before_cleanup(void)
{
    assert_failed_init(CAN_APP_INIT_NOTIFICATION_ERROR,
                       TEST_INIT_STAGE_NOTIFICATION);
    TEST_ASSERT_EQUAL_UINT32(1U, calls.notification);
    TEST_ASSERT_EQUAL_UINT32(1U, calls.stop);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_success_configures_filters_fifo_and_enables_transport);
    RUN_TEST(test_unavailable_or_null_peripheral_never_touches_hal);
    RUN_TEST(test_filter_failure_disables_transport_and_stops_sequence);
    RUN_TEST(test_global_filter_failure_stops_before_fifo_configuration);
    RUN_TEST(test_fifo_mode_failure_stops_before_watermark);
    RUN_TEST(test_watermark_failure_stops_before_start);
    RUN_TEST(test_start_failure_does_not_enable_notifications);
    RUN_TEST(test_notification_failure_stops_controller_before_cleanup);
    return UNITY_END();
}
