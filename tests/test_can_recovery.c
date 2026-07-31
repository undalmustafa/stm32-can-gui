#include "unity.h"

#include "app_log.h"
#include "can_recovery.h"

#define TEST_LOG_CAPACITY 16U
#define TEST_NOTIFICATION_MASK \
    (FDCAN_IT_BUS_OFF | FDCAN_IT_ERROR_PASSIVE | \
     FDCAN_IT_TX_COMPLETE | FDCAN_IT_RX_FIFO0_NEW_MESSAGE | \
     FDCAN_IT_RX_FIFO0_WATERMARK | FDCAN_IT_RX_FIFO0_FULL | \
     FDCAN_IT_RX_FIFO0_MESSAGE_LOST)

typedef struct
{
    App_Log_Severity_t severity;
    App_Log_EventCode_t event_code;
    uint32_t data_0;
    uint32_t data_1;
} Test_LogRecord_t;

FDCAN_GlobalTypeDef test_fdcan1_instance;
FDCAN_HandleTypeDef hfdcan1;

static uint32_t fake_tick;
static HAL_StatusTypeDef stop_status;
static HAL_StatusTypeDef start_status;
static HAL_StatusTypeDef notification_status;
static uint32_t stop_calls;
static uint32_t start_calls;
static uint32_t notification_calls;
static uint32_t notification_mask;
static uint32_t notification_buffer_mask;
static Test_LogRecord_t log_records[TEST_LOG_CAPACITY];
static uint32_t log_count;

void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan,
                                   uint32_t ErrorStatusITs);
void HAL_FDCAN_TxBufferCompleteCallback(FDCAN_HandleTypeDef *hfdcan,
                                        uint32_t BufferIndexes);

uint32_t HAL_GetTick(void)
{
    return fake_tick;
}

HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef *hfdcan)
{
    TEST_ASSERT_EQUAL_PTR(&hfdcan1, hfdcan);
    stop_calls++;
    return stop_status;
}

HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *hfdcan)
{
    TEST_ASSERT_EQUAL_PTR(&hfdcan1, hfdcan);
    start_calls++;
    return start_status;
}

HAL_StatusTypeDef HAL_FDCAN_ActivateNotification(
    FDCAN_HandleTypeDef *hfdcan,
    uint32_t ActiveITs,
    uint32_t BufferIndexes)
{
    TEST_ASSERT_EQUAL_PTR(&hfdcan1, hfdcan);
    notification_calls++;
    notification_mask = ActiveITs;
    notification_buffer_mask = BufferIndexes;
    return notification_status;
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
        log_records[log_count].severity = severity;
        log_records[log_count].event_code = event_code;
        log_records[log_count].data_0 = data_0;
        log_records[log_count].data_1 = data_1;
        log_count++;
        return 1U;
    }

    return 0U;
}

static void inject_error(uint32_t flags, uint32_t psr, uint32_t ecr)
{
    test_fdcan1_instance.PSR = psr;
    test_fdcan1_instance.ECR = ecr;
    HAL_FDCAN_ErrorStatusCallback(&hfdcan1, flags);
}

static void process_at(uint32_t tick)
{
    fake_tick = tick;
    CAN_Handle_BusOff_Recovery();
}

void setUp(void)
{
    uint32_t index;

    test_fdcan1_instance = (FDCAN_GlobalTypeDef){0};
    hfdcan1.Instance = FDCAN1;
    fake_tick = 0U;
    stop_status = HAL_OK;
    start_status = HAL_OK;
    notification_status = HAL_OK;
    stop_calls = 0U;
    start_calls = 0U;
    notification_calls = 0U;
    notification_mask = 0U;
    notification_buffer_mask = 0U;
    log_count = 0U;
    for (index = 0U; index < TEST_LOG_CAPACITY; index++)
    {
        log_records[index] = (Test_LogRecord_t){0};
    }
    CAN_Recovery_Init();
}

void tearDown(void)
{
}

void test_bus_off_recovery_waits_for_rate_limit_and_tx_confirmation(void)
{
    CAN_Recovery_Stats_t stats;

    fake_tick = 10U;
    inject_error(FDCAN_IT_BUS_OFF, 0x11223344U, 0x55667788U);
    CAN_Handle_BusOff_Recovery();

    TEST_ASSERT_EQUAL_UINT32(1U, log_count);
    TEST_ASSERT_EQUAL(APP_LOG_EVENT_CAN_BUS_OFF,
                      log_records[0].event_code);
    TEST_ASSERT_EQUAL_UINT32(0x11223344U, log_records[0].data_0);
    TEST_ASSERT_EQUAL_UINT32(0x55667788U, log_records[0].data_1);

    process_at(209U);
    TEST_ASSERT_EQUAL_UINT32(0U, stop_calls);

    process_at(210U);
    CAN_Recovery_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stop_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, start_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, notification_calls);
    TEST_ASSERT_EQUAL_HEX32(TEST_NOTIFICATION_MASK, notification_mask);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFUL, notification_buffer_mask);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.recovery_attempts);
    TEST_ASSERT_EQUAL_UINT8(1U, stats.verification_pending);
    TEST_ASSERT_EQUAL_UINT32(0U, stats.recovery_successes);

    HAL_FDCAN_TxBufferCompleteCallback(&hfdcan1, FDCAN_TX_BUFFER0);
    CAN_Handle_BusOff_Recovery();
    CAN_Recovery_GetStats(&stats);

    TEST_ASSERT_EQUAL_UINT32(1U, stats.recovery_successes);
    TEST_ASSERT_EQUAL_UINT8(0U, stats.recovery_pending);
    TEST_ASSERT_EQUAL_UINT32(2U, log_count);
    TEST_ASSERT_EQUAL(APP_LOG_EVENT_CAN_RECOVERY_OK,
                      log_records[1].event_code);
}

void test_repeated_stop_failure_is_rate_limited_and_logged_once(void)
{
    CAN_Recovery_Stats_t stats;

    inject_error(FDCAN_IT_BUS_OFF, 1U, 2U);
    CAN_Handle_BusOff_Recovery();
    stop_status = HAL_ERROR;

    process_at(200U);
    CAN_Recovery_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stop_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.recovery_failures);
    TEST_ASSERT_EQUAL(CAN_RECOVERY_STEP_STOP, stats.last_failed_step);

    process_at(399U);
    TEST_ASSERT_EQUAL_UINT32(1U, stop_calls);
    process_at(400U);
    CAN_Recovery_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(2U, stop_calls);
    TEST_ASSERT_EQUAL_UINT32(2U, stats.recovery_failures);
    TEST_ASSERT_EQUAL_UINT32(2U, stats.recovery_attempts);
    TEST_ASSERT_EQUAL_UINT32(2U, log_count);
    TEST_ASSERT_EQUAL(APP_LOG_EVENT_CAN_RECOVERY_FAILED,
                      log_records[1].event_code);

    stop_status = HAL_OK;
    process_at(600U);
    TEST_ASSERT_EQUAL_UINT32(3U, stop_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, start_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, notification_calls);
}

void test_start_failure_resumes_without_repeating_stop(void)
{
    CAN_Recovery_Stats_t stats;

    inject_error(FDCAN_IT_BUS_OFF, 1U, 2U);
    CAN_Handle_BusOff_Recovery();
    start_status = HAL_BUSY;

    process_at(200U);
    CAN_Recovery_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stop_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, start_calls);
    TEST_ASSERT_EQUAL(CAN_RECOVERY_STEP_START, stats.last_failed_step);

    start_status = HAL_OK;
    process_at(400U);
    TEST_ASSERT_EQUAL_UINT32(1U, stop_calls);
    TEST_ASSERT_EQUAL_UINT32(2U, start_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, notification_calls);
}

void test_notification_failure_resumes_without_repeating_stop_or_start(void)
{
    inject_error(FDCAN_IT_BUS_OFF, 1U, 2U);
    CAN_Handle_BusOff_Recovery();
    notification_status = HAL_TIMEOUT;

    process_at(200U);
    TEST_ASSERT_EQUAL_UINT32(1U, stop_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, start_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, notification_calls);

    notification_status = HAL_OK;
    process_at(400U);
    TEST_ASSERT_EQUAL_UINT32(1U, stop_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, start_calls);
    TEST_ASSERT_EQUAL_UINT32(2U, notification_calls);
}

void test_new_bus_off_rejects_stale_tx_confirmation(void)
{
    CAN_Recovery_Stats_t stats;

    inject_error(FDCAN_IT_BUS_OFF, 1U, 2U);
    CAN_Handle_BusOff_Recovery();
    process_at(200U);

    HAL_FDCAN_TxBufferCompleteCallback(&hfdcan1, FDCAN_TX_BUFFER0);
    fake_tick = 201U;
    inject_error(FDCAN_IT_BUS_OFF, 3U, 4U);
    CAN_Handle_BusOff_Recovery();
    CAN_Recovery_GetStats(&stats);

    TEST_ASSERT_EQUAL_UINT32(0U, stats.recovery_successes);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.verification_failures);
    TEST_ASSERT_EQUAL_UINT32(2U, stats.bus_off_events);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.bus_off_log_records);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.bus_off_log_suppressed);

    process_at(401U);
    HAL_FDCAN_TxBufferCompleteCallback(&hfdcan1, FDCAN_TX_BUFFER0);
    CAN_Handle_BusOff_Recovery();
    CAN_Recovery_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.recovery_successes);
    TEST_ASSERT_EQUAL_UINT8(0U, stats.recovery_pending);
}

void test_error_passive_logging_is_coalesced_during_bus_off_episode(void)
{
    CAN_Recovery_Stats_t stats;

    inject_error(FDCAN_IT_ERROR_PASSIVE, 0x10U, 0x20U);
    CAN_Handle_BusOff_Recovery();
    TEST_ASSERT_EQUAL(APP_LOG_EVENT_CAN_ERROR_PASSIVE,
                      log_records[0].event_code);

    inject_error(FDCAN_IT_BUS_OFF, 0x30U, 0x40U);
    CAN_Handle_BusOff_Recovery();
    inject_error(FDCAN_IT_ERROR_PASSIVE, 0x50U, 0x60U);
    inject_error(FDCAN_IT_ERROR_PASSIVE, 0x70U, 0x80U);
    CAN_Handle_BusOff_Recovery();
    CAN_Recovery_GetStats(&stats);

    TEST_ASSERT_EQUAL_UINT32(1U, stats.error_passive_log_records);
    TEST_ASSERT_EQUAL_UINT32(2U, stats.error_passive_log_suppressed);
    TEST_ASSERT_EQUAL_UINT32(2U, log_count);
}

void test_retry_deadline_is_safe_across_tick_wrap(void)
{
    CAN_Recovery_Stats_t stats;
    uint32_t event_tick = UINT32_MAX - 100U;

    fake_tick = event_tick;
    inject_error(FDCAN_IT_BUS_OFF, 1U, 2U);
    CAN_Handle_BusOff_Recovery();

    process_at(98U);
    TEST_ASSERT_EQUAL_UINT32(0U, stop_calls);
    process_at(99U);
    CAN_Recovery_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stop_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.recovery_attempts);
}

void test_invalid_callbacks_are_ignored_and_init_clears_state(void)
{
    CAN_Recovery_Stats_t stats;
    FDCAN_GlobalTypeDef other_instance = {0};
    FDCAN_HandleTypeDef other_handle = {&other_instance};

    HAL_FDCAN_ErrorStatusCallback(NULL, FDCAN_IT_BUS_OFF);
    HAL_FDCAN_ErrorStatusCallback(&other_handle, FDCAN_IT_BUS_OFF);
    HAL_FDCAN_TxBufferCompleteCallback(NULL, FDCAN_TX_BUFFER0);
    HAL_FDCAN_TxBufferCompleteCallback(&other_handle, FDCAN_TX_BUFFER0);
    HAL_FDCAN_TxBufferCompleteCallback(&hfdcan1, 0U);
    CAN_Recovery_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(0U, stats.bus_off_events);
    TEST_ASSERT_EQUAL_UINT32(0U, stats.tx_complete_events);

    inject_error(FDCAN_IT_BUS_OFF, 1U, 2U);
    HAL_FDCAN_TxBufferCompleteCallback(&hfdcan1, FDCAN_TX_BUFFER0);
    CAN_Recovery_Init();
    CAN_Recovery_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(0U, stats.bus_off_events);
    TEST_ASSERT_EQUAL_UINT32(0U, stats.tx_complete_events);
    TEST_ASSERT_EQUAL_UINT8(0U, stats.recovery_pending);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(
        test_bus_off_recovery_waits_for_rate_limit_and_tx_confirmation);
    RUN_TEST(test_repeated_stop_failure_is_rate_limited_and_logged_once);
    RUN_TEST(test_start_failure_resumes_without_repeating_stop);
    RUN_TEST(
        test_notification_failure_resumes_without_repeating_stop_or_start);
    RUN_TEST(test_new_bus_off_rejects_stale_tx_confirmation);
    RUN_TEST(
        test_error_passive_logging_is_coalesced_during_bus_off_episode);
    RUN_TEST(test_retry_deadline_is_safe_across_tick_wrap);
    RUN_TEST(test_invalid_callbacks_are_ignored_and_init_clears_state);
    return UNITY_END();
}
