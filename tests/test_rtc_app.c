#include "unity.h"

#include "../Core/Inc/rtc_app.h"
#include "app_log.h"
#include "can_transport.h"

#include <limits.h>
#include <string.h>

#define TEST_TX_CAPACITY  24U
#define TEST_LOG_CAPACITY 24U

typedef enum
{
    TEST_TX_LATEST = 0,
    TEST_TX_HIGH_PRIORITY
} Test_TxKind_t;

typedef struct
{
    uint32_t identifier;
    CAN_Transport_IdType_t id_type;
    Test_TxKind_t kind;
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE];
} Test_TxFrame_t;

typedef struct
{
    App_Log_Source_t source;
    App_Log_Severity_t severity;
    App_Log_EventCode_t event;
    uint32_t data_0;
    uint32_t data_1;
} Test_LogRecord_t;

I2C_HandleTypeDef hi2c1;

static uint32_t fake_tick;
static Test_TxFrame_t tx_frames[TEST_TX_CAPACITY];
static uint32_t tx_count;
static Test_LogRecord_t log_records[TEST_LOG_CAPACITY];
static uint32_t log_count;
static CAN_Transport_Result_t latest_tx_result;
static CAN_Transport_Result_t high_priority_tx_result;

static PCA2131_OperationStatus_t check_ready_status;
static PCA2131_OperationStatus_t read_datetime_status;
static PCA2131_OperationStatus_t write_datetime_status;
static PCA2131_OperationStatus_t read_alarm_status;
static PCA2131_OperationStatus_t write_alarm_status;
static PCA2131_OperationStatus_t alarm_status_read_status;
static PCA2131_OperationStatus_t clear_alarm_status;
static PCA2131_OperationStatus_t watchdog_status_read_status;
static PCA2131_DateTime_t read_datetime_value;
static PCA2131_DateTime_t written_datetime;
static PCA2131_Alarm_t read_alarm_value;
static PCA2131_AlarmConfig_t written_alarm;
static PCA2131_AlarmStatus_t alarm_status_value;
static PCA2131_WatchdogStatus_t watchdog_status_value;
static uint8_t alarm_config_valid;

static uint32_t driver_init_calls;
static uint32_t check_ready_calls;
static uint32_t read_datetime_calls;
static uint32_t write_datetime_calls;
static uint32_t read_alarm_calls;
static uint32_t write_alarm_calls;
static uint32_t read_alarm_status_calls;
static uint32_t clear_alarm_calls;
static uint32_t read_watchdog_calls;

static PCA2131_OperationStatus_t status_ok(void)
{
    PCA2131_OperationStatus_t status = {0};

    status.result = PCA2131_RESULT_OK;
    status.hal_status = HAL_OK;
    status.recovery_status = HAL_OK;
    return status;
}

uint32_t HAL_GetTick(void)
{
    return fake_tick;
}

void PCA2131_Driver_Init(PCA2131_Device_t *device,
                         I2C_HandleTypeDef *i2c)
{
    driver_init_calls++;
    TEST_ASSERT_EQUAL_PTR(&hi2c1, i2c);
    device->i2c = i2c;
}

uint8_t PCA2131_Driver_IsValidDateTime(
    const PCA2131_DateTime_t *date_time)
{
    return (date_time != NULL) ? 1U : 0U;
}

PCA2131_OperationStatus_t PCA2131_Driver_CheckReady(
    const PCA2131_Device_t *device)
{
    check_ready_calls++;
    TEST_ASSERT_EQUAL_PTR(&hi2c1, device->i2c);
    return check_ready_status;
}

PCA2131_OperationStatus_t PCA2131_Driver_ReadDateTime(
    const PCA2131_Device_t *device,
    PCA2131_DateTime_t *date_time)
{
    read_datetime_calls++;
    TEST_ASSERT_EQUAL_PTR(&hi2c1, device->i2c);
    *date_time = read_datetime_value;
    return read_datetime_status;
}

PCA2131_OperationStatus_t PCA2131_Driver_WriteDateTime(
    const PCA2131_Device_t *device,
    const PCA2131_DateTime_t *date_time)
{
    write_datetime_calls++;
    TEST_ASSERT_EQUAL_PTR(&hi2c1, device->i2c);
    written_datetime = *date_time;
    return write_datetime_status;
}

PCA2131_OperationStatus_t PCA2131_Driver_ReadAlarm(
    const PCA2131_Device_t *device,
    PCA2131_Alarm_t *alarm)
{
    read_alarm_calls++;
    TEST_ASSERT_EQUAL_PTR(&hi2c1, device->i2c);
    *alarm = read_alarm_value;
    return read_alarm_status;
}

uint8_t PCA2131_Driver_IsValidAlarmConfig(
    const PCA2131_AlarmConfig_t *config)
{
    return ((config != NULL) && (alarm_config_valid != 0U)) ? 1U : 0U;
}

PCA2131_OperationStatus_t PCA2131_Driver_WriteAlarm(
    const PCA2131_Device_t *device,
    const PCA2131_AlarmConfig_t *config)
{
    write_alarm_calls++;
    TEST_ASSERT_EQUAL_PTR(&hi2c1, device->i2c);
    written_alarm = *config;
    return write_alarm_status;
}

PCA2131_OperationStatus_t PCA2131_Driver_ReadAlarmStatus(
    const PCA2131_Device_t *device,
    PCA2131_AlarmStatus_t *status)
{
    read_alarm_status_calls++;
    TEST_ASSERT_EQUAL_PTR(&hi2c1, device->i2c);
    *status = alarm_status_value;
    return alarm_status_read_status;
}

PCA2131_OperationStatus_t PCA2131_Driver_ClearAlarmFlag(
    const PCA2131_Device_t *device)
{
    clear_alarm_calls++;
    TEST_ASSERT_EQUAL_PTR(&hi2c1, device->i2c);
    return clear_alarm_status;
}

PCA2131_OperationStatus_t PCA2131_Driver_ReadWatchdogStatus(
    const PCA2131_Device_t *device,
    PCA2131_WatchdogStatus_t *status)
{
    read_watchdog_calls++;
    TEST_ASSERT_EQUAL_PTR(&hi2c1, device->i2c);
    *status = watchdog_status_value;
    return watchdog_status_read_status;
}

static CAN_Transport_Result_t capture_tx(
    uint32_t identifier,
    CAN_Transport_IdType_t id_type,
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE],
    Test_TxKind_t kind,
    CAN_Transport_Result_t result)
{
    if (tx_count < TEST_TX_CAPACITY)
    {
        tx_frames[tx_count].identifier = identifier;
        tx_frames[tx_count].id_type = id_type;
        tx_frames[tx_count].kind = kind;
        (void)memcpy(tx_frames[tx_count].payload,
                     payload,
                     CAN_PROTOCOL_PAYLOAD_SIZE);
        tx_count++;
    }

    return result;
}

CAN_Transport_Result_t CAN_Transport_SendClassicLatest(
    uint32_t identifier,
    CAN_Transport_IdType_t id_type,
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    return capture_tx(identifier,
                      id_type,
                      payload,
                      TEST_TX_LATEST,
                      latest_tx_result);
}

CAN_Transport_Result_t CAN_Transport_SendClassicHighPriority(
    uint32_t identifier,
    CAN_Transport_IdType_t id_type,
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    return capture_tx(identifier,
                      id_type,
                      payload,
                      TEST_TX_HIGH_PRIORITY,
                      high_priority_tx_result);
}

uint8_t App_Log_Push(App_Log_Source_t source,
                     App_Log_Severity_t severity,
                     App_Log_EventCode_t event_code,
                     uint32_t data_0,
                     uint32_t data_1)
{
    if (log_count < TEST_LOG_CAPACITY)
    {
        log_records[log_count].source = source;
        log_records[log_count].severity = severity;
        log_records[log_count].event = event_code;
        log_records[log_count].data_0 = data_0;
        log_records[log_count].data_1 = data_1;
        log_count++;
        return 1U;
    }

    return 0U;
}

static void reset_activity(void)
{
    tx_count = 0U;
    log_count = 0U;
    driver_init_calls = 0U;
    check_ready_calls = 0U;
    read_datetime_calls = 0U;
    write_datetime_calls = 0U;
    read_alarm_calls = 0U;
    write_alarm_calls = 0U;
    read_alarm_status_calls = 0U;
    clear_alarm_calls = 0U;
    read_watchdog_calls = 0U;
    (void)memset(tx_frames, 0, sizeof(tx_frames));
    (void)memset(log_records, 0, sizeof(log_records));
}

static void reset_fixture(void)
{
    fake_tick = 0U;
    hi2c1 = (I2C_HandleTypeDef){0};
    latest_tx_result = CAN_TRANSPORT_OK;
    high_priority_tx_result = CAN_TRANSPORT_OK;
    check_ready_status = status_ok();
    read_datetime_status = status_ok();
    write_datetime_status = status_ok();
    read_alarm_status = status_ok();
    write_alarm_status = status_ok();
    alarm_status_read_status = status_ok();
    clear_alarm_status = status_ok();
    watchdog_status_read_status = status_ok();
    read_datetime_value = (PCA2131_DateTime_t){0};
    written_datetime = (PCA2131_DateTime_t){0};
    read_alarm_value = (PCA2131_Alarm_t){0};
    written_alarm = (PCA2131_AlarmConfig_t){0};
    alarm_status_value = (PCA2131_AlarmStatus_t){0};
    watchdog_status_value = (PCA2131_WatchdogStatus_t){0};
    alarm_config_valid = 1U;

    PCA2131_Init_Check(0U);
    reset_activity();
}

void setUp(void)
{
    reset_fixture();
}

void tearDown(void)
{
}

static void start_ready(uint32_t start_tick)
{
    fake_tick = start_tick;
    PCA2131_Init_Check(1U);

    fake_tick = start_tick + 49U;
    RTC_Process();
    TEST_ASSERT_EQUAL_UINT32(0U, check_ready_calls);

    fake_tick = start_tick + 50U;
    RTC_Process();
    TEST_ASSERT_EQUAL_UINT32(1U, check_ready_calls);

    RTC_Process();
    RTC_Process();
}

static const Test_TxFrame_t *find_tx(uint32_t identifier,
                                     uint32_t occurrence)
{
    uint32_t index;

    for (index = 0U; index < tx_count; index++)
    {
        if (tx_frames[index].identifier == identifier)
        {
            if (occurrence == 0U)
            {
                return &tx_frames[index];
            }
            occurrence--;
        }
    }

    return NULL;
}

static uint32_t count_tx(uint32_t identifier)
{
    uint32_t count = 0U;
    uint32_t index;

    for (index = 0U; index < tx_count; index++)
    {
        if (tx_frames[index].identifier == identifier)
        {
            count++;
        }
    }

    return count;
}

static PCA2131_DateTime_t sample_datetime(void)
{
    PCA2131_DateTime_t date_time = {0};

    date_time.hundredth = 12U;
    date_time.second = 34U;
    date_time.minute = 56U;
    date_time.hour = 17U;
    date_time.day = 28U;
    date_time.weekday = 2U;
    date_time.month = 7U;
    date_time.year = 26U;
    date_time.calendar_valid = 1U;
    return date_time;
}

static PCA2131_AlarmConfig_t sample_alarm_config(void)
{
    PCA2131_AlarmConfig_t config = {0};

    config.second.enabled = 1U;
    config.second.value = 5U;
    config.minute.enabled = 1U;
    config.minute.value = 4U;
    config.hour.enabled = 1U;
    config.hour.value = 13U;
    config.day.enabled = 1U;
    config.day.value = 3U;
    config.weekday.enabled = 1U;
    config.weekday.value = 2U;
    return config;
}

static PCA2131_Alarm_t alarm_from_config(
    const PCA2131_AlarmConfig_t *config)
{
    PCA2131_Alarm_t alarm = {0};

    alarm.second = config->second;
    alarm.minute = config->minute;
    alarm.hour = config->hour;
    alarm.day = config->day;
    alarm.weekday = config->weekday;
    alarm.configuration_valid = 1U;
    return alarm;
}

void test_initialization_deadline_is_tick_wrap_safe(void)
{
    const Test_TxFrame_t *status_frame;

    fake_tick = UINT32_MAX - 25U;
    PCA2131_Init_Check(1U);

    TEST_ASSERT_EQUAL_UINT32(1U, driver_init_calls);
    fake_tick = 23U;
    RTC_Process();
    TEST_ASSERT_EQUAL_UINT32(0U, check_ready_calls);

    fake_tick = 24U;
    RTC_Process();
    TEST_ASSERT_EQUAL_UINT32(1U, check_ready_calls);
    status_frame = find_tx(CAN_PROTOCOL_RTC_STATUS_TX_ID, 0U);
    TEST_ASSERT_NOT_NULL(status_frame);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_RTC_STATUS_INIT_OK,
                            status_frame->payload[0]);
    TEST_ASSERT_EQUAL(APP_LOG_EVENT_RTC_INIT_OK, log_records[0].event);
}

void test_startup_discovers_watchdog_and_alarm_without_mutating_them(void)
{
    RTC_WatchdogSnapshot_t watchdog_snapshot;
    RTC_AlarmSnapshot_t alarm_snapshot;

    watchdog_status_value.enabled = 1U;
    watchdog_status_value.timeout_flag = 1U;
    read_alarm_value.configuration_valid = 1U;
    read_alarm_value.minute.enabled = 1U;
    read_alarm_value.minute.value = 30U;

    start_ready(100U);
    RTC_GetWatchdogSnapshot(&watchdog_snapshot);
    RTC_GetAlarmSnapshot(&alarm_snapshot);

    TEST_ASSERT_EQUAL_UINT32(1U, read_watchdog_calls);
    TEST_ASSERT_EQUAL_UINT8(1U, watchdog_snapshot.read_attempted);
    TEST_ASSERT_EQUAL_UINT8(1U, watchdog_snapshot.read_ok);
    TEST_ASSERT_EQUAL_UINT8(1U, watchdog_snapshot.status.enabled);
    TEST_ASSERT_EQUAL_UINT32(1U, read_alarm_calls);
    TEST_ASSERT_EQUAL_UINT8(1U, alarm_snapshot.read_ok);
    TEST_ASSERT_EQUAL_UINT8(30U, alarm_snapshot.alarm.minute.value);
    TEST_ASSERT_EQUAL_UINT32(0U, clear_alarm_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, write_alarm_calls);
}

void test_failed_init_retries_slowly_and_reports_reconnection_once(void)
{
    const Test_TxFrame_t *status_frame;

    check_ready_status.result = PCA2131_RESULT_DEVICE_NOT_READY;
    check_ready_status.hal_status = HAL_ERROR;
    check_ready_status.hal_error = 0x51U;
    fake_tick = 500U;
    PCA2131_Init_Check(1U);
    fake_tick = 550U;
    RTC_Process();
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_RTC_STATUS_INIT_FAILED,
                            tx_frames[0].payload[0]);

    reset_activity();
    read_datetime_value = sample_datetime();
    fake_tick = 1499U;
    RTC_Process();
    TEST_ASSERT_EQUAL_UINT32(0U, read_datetime_calls);

    fake_tick = 1500U;
    RTC_Process();
    TEST_ASSERT_EQUAL_UINT32(1U, read_datetime_calls);
    status_frame = find_tx(CAN_PROTOCOL_RTC_STATUS_TX_ID, 0U);
    TEST_ASSERT_NOT_NULL(status_frame);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_RTC_STATUS_RECONNECTED,
                            status_frame->payload[0]);
    TEST_ASSERT_EQUAL_UINT32(1U,
        count_tx(CAN_PROTOCOL_RTC_TIME_TX_ID));
    TEST_ASSERT_EQUAL(APP_LOG_EVENT_RTC_RECOVERED, log_records[0].event);
}

void test_datetime_write_blocks_overlap_and_verifies_matching_readback(void)
{
    PCA2131_DateTime_t requested = sample_datetime();
    const Test_TxFrame_t *status_frame;
    const Test_TxFrame_t *time_frame;

    requested.hundredth = 99U;
    requested.second = 59U;
    start_ready(0U);
    reset_activity();
    fake_tick = 200U;
    TEST_ASSERT_EQUAL_UINT8(1U,
        PCA2131_SetDateTime(requested.hundredth,
                            requested.second,
                            requested.minute,
                            requested.hour,
                            requested.day,
                            requested.weekday,
                            requested.month,
                            requested.year));
    TEST_ASSERT_EQUAL_UINT8(0U,
        PCA2131_SetDateTime(requested.hundredth,
                            requested.second,
                            requested.minute,
                            requested.hour,
                            requested.day,
                            requested.weekday,
                            requested.month,
                            requested.year));
    TEST_ASSERT_EQUAL_UINT32(1U, write_datetime_calls);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_RTC_STATUS_BUSY,
                            tx_frames[0].payload[0]);

    reset_activity();
    read_datetime_value = requested;
    read_datetime_value.hundredth = 1U;
    read_datetime_value.second = 0U;
    read_datetime_value.minute++;
    fake_tick = 219U;
    RTC_Process();
    TEST_ASSERT_EQUAL_UINT32(0U, read_datetime_calls);

    fake_tick = 220U;
    RTC_Process();
    TEST_ASSERT_EQUAL_UINT32(1U, read_datetime_calls);
    status_frame = find_tx(CAN_PROTOCOL_RTC_STATUS_TX_ID, 0U);
    time_frame = find_tx(CAN_PROTOCOL_RTC_TIME_TX_ID, 0U);
    TEST_ASSERT_NOT_NULL(status_frame);
    TEST_ASSERT_NOT_NULL(time_frame);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_RTC_STATUS_WRITE_VERIFY_OK,
                            status_frame->payload[0]);
    TEST_ASSERT_EQUAL_UINT8(requested.hour, time_frame->payload[0]);
    TEST_ASSERT_EQUAL_UINT8(read_datetime_value.hundredth,
                            time_frame->payload[3]);
    TEST_ASSERT_EQUAL(APP_LOG_EVENT_RTC_WRITE_OK, log_records[0].event);
}

void test_datetime_readback_mismatch_is_not_reported_as_success(void)
{
    PCA2131_DateTime_t requested = sample_datetime();
    const Test_TxFrame_t *status_frame;
    const Test_TxFrame_t *time_frame;

    start_ready(0U);
    reset_activity();
    fake_tick = 300U;
    TEST_ASSERT_EQUAL_UINT8(1U,
        PCA2131_SetDateTime(requested.hundredth,
                            requested.second,
                            requested.minute,
                            requested.hour,
                            requested.day,
                            requested.weekday,
                            requested.month,
                            requested.year));

    read_datetime_value = requested;
    read_datetime_value.minute++;
    fake_tick = 320U;
    RTC_Process();

    status_frame = find_tx(CAN_PROTOCOL_RTC_STATUS_TX_ID, 0U);
    time_frame = find_tx(CAN_PROTOCOL_RTC_TIME_TX_ID, 0U);
    TEST_ASSERT_NOT_NULL(status_frame);
    TEST_ASSERT_NOT_NULL(time_frame);
    TEST_ASSERT_EQUAL_UINT8(
        CAN_PROTOCOL_RTC_STATUS_WRITE_VERIFY_MISMATCH,
        status_frame->payload[0]);
    TEST_ASSERT_EQUAL_UINT8(read_datetime_value.minute,
                            time_frame->payload[1]);
    TEST_ASSERT_EQUAL(APP_LOG_SEVERITY_FAULT, log_records[0].severity);
    TEST_ASSERT_EQUAL(APP_LOG_EVENT_RTC_WRITE_FAILED,
                      log_records[0].event);
}

void test_reinitialization_cancels_pending_datetime_readback(void)
{
    PCA2131_DateTime_t requested = sample_datetime();

    start_ready(0U);
    reset_activity();
    fake_tick = 400U;
    TEST_ASSERT_EQUAL_UINT8(1U,
        PCA2131_SetDateTime(requested.hundredth,
                            requested.second,
                            requested.minute,
                            requested.hour,
                            requested.day,
                            requested.weekday,
                            requested.month,
                            requested.year));

    fake_tick = 401U;
    PCA2131_Init_Check(1U);
    reset_activity();
    fake_tick = 451U;
    RTC_Process();
    TEST_ASSERT_EQUAL_UINT32(1U, check_ready_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, read_datetime_calls);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_RTC_STATUS_INIT_OK,
                            tx_frames[0].payload[0]);

    RTC_Process();
    TEST_ASSERT_EQUAL_UINT32(1U, read_watchdog_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, read_datetime_calls);
}

void test_datetime_driver_and_recovery_failures_are_both_reported(void)
{
    PCA2131_DateTime_t requested = sample_datetime();

    start_ready(0U);
    reset_activity();
    write_datetime_status.result = PCA2131_RESULT_CALENDAR_WRITE_FAILED;
    write_datetime_status.hal_status = HAL_ERROR;
    write_datetime_status.hal_error = 0x11223344UL;
    write_datetime_status.recovery_attempted = 1U;
    write_datetime_status.recovery_status = HAL_TIMEOUT;
    write_datetime_status.recovery_error = 0x55667788UL;

    TEST_ASSERT_EQUAL_UINT8(0U,
        PCA2131_SetDateTime(requested.hundredth,
                            requested.second,
                            requested.minute,
                            requested.hour,
                            requested.day,
                            requested.weekday,
                            requested.month,
                            requested.year));

    TEST_ASSERT_EQUAL_UINT32(2U,
        count_tx(CAN_PROTOCOL_RTC_STATUS_TX_ID));
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_RTC_STATUS_CALENDAR_WRITE_FAILED,
                            tx_frames[0].payload[0]);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_RTC_STATUS_RECOVERY_FAILED,
                            tx_frames[1].payload[0]);
    TEST_ASSERT_EQUAL(APP_LOG_EVENT_RTC_WRITE_FAILED,
                      log_records[0].event);
    TEST_ASSERT_EQUAL_HEX32(write_datetime_status.recovery_error,
                            log_records[0].data_1);
}

void test_alarm_write_verification_handles_12_hour_readback_and_tick_wrap(void)
{
    PCA2131_AlarmConfig_t config = sample_alarm_config();
    RTC_AlarmWriteStatus_t status;

    start_ready(0U);
    reset_activity();
    read_alarm_value = alarm_from_config(&config);
    read_alarm_value.hour.value = 1U;
    read_alarm_value.hour_mode_12 = 1U;
    read_alarm_value.hour_pm = 1U;

    fake_tick = UINT32_MAX;
    TEST_ASSERT_EQUAL_UINT8(1U, RTC_SetAlarm(&config));
    TEST_ASSERT_EQUAL_UINT32(1U, clear_alarm_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, write_alarm_calls);
    TEST_ASSERT_EQUAL_MEMORY(&config, &written_alarm, sizeof(config));

    RTC_Process();
    TEST_ASSERT_EQUAL_UINT32(0U, read_alarm_calls);
    fake_tick = 0U;
    RTC_Process();
    RTC_GetAlarmWriteStatus(&status);

    TEST_ASSERT_EQUAL(RTC_ALARM_WRITE_VERIFIED, status.result);
    TEST_ASSERT_EQUAL_UINT32(1U, status.verified_count);
    TEST_ASSERT_EQUAL_UINT32(0U, status.failure_count);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_RTC_STATUS_ALARM_WRITE_VERIFY_OK,
                            tx_frames[0].payload[0]);
    TEST_ASSERT_EQUAL(APP_LOG_EVENT_ALARM_CONFIGURED,
                      log_records[0].event);
}

void test_alarm_readback_mismatch_is_latched_as_failure(void)
{
    PCA2131_AlarmConfig_t config = sample_alarm_config();
    RTC_AlarmWriteStatus_t status;

    start_ready(0U);
    reset_activity();
    read_alarm_value = alarm_from_config(&config);
    read_alarm_value.minute.value++;
    fake_tick = 600U;
    TEST_ASSERT_EQUAL_UINT8(1U, RTC_SetAlarm(&config));
    fake_tick = 601U;
    RTC_Process();
    RTC_GetAlarmWriteStatus(&status);

    TEST_ASSERT_EQUAL(RTC_ALARM_WRITE_VERIFY_MISMATCH, status.result);
    TEST_ASSERT_EQUAL_UINT32(1U, status.failure_count);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_RTC_STATUS_ALARM_VERIFY_MISMATCH,
                            tx_frames[0].payload[0]);
    TEST_ASSERT_EQUAL(APP_LOG_SEVERITY_FAULT, log_records[0].severity);
    TEST_ASSERT_EQUAL(APP_LOG_EVENT_ALARM_FAILED,
                      log_records[0].event);
}

void test_alarm_event_retries_failed_transport_then_clears_flag(void)
{
    RTC_AlarmEventDiagnostics_t diagnostics;
    const Test_TxFrame_t *event_frame;

    read_alarm_value.configuration_valid = 1U;
    read_alarm_value.minute.enabled = 1U;
    read_alarm_value.minute.value = 4U;
    start_ready(0U);
    reset_activity();

    read_datetime_value = sample_datetime();
    alarm_status_value.alarm_flag = 1U;
    alarm_status_value.interrupt_enabled = 1U;
    high_priority_tx_result = CAN_TRANSPORT_QUEUE_FULL;
    fake_tick = 100U;
    RTC_Process();
    TEST_ASSERT_EQUAL_UINT32(1U, read_alarm_status_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, clear_alarm_calls);

    high_priority_tx_result = CAN_TRANSPORT_OK;
    fake_tick = 200U;
    RTC_Process();
    RTC_GetAlarmEventDiagnostics(&diagnostics);

    TEST_ASSERT_EQUAL_UINT32(2U, diagnostics.poll_count);
    TEST_ASSERT_EQUAL_UINT32(1U, diagnostics.tx_failure_count);
    TEST_ASSERT_EQUAL_UINT32(1U, diagnostics.event_count);
    TEST_ASSERT_EQUAL_UINT32(1U, diagnostics.flag_clear_count);
    TEST_ASSERT_EQUAL_UINT32(2U,
        count_tx(CAN_PROTOCOL_RTC_ALARM_EVENT_TX_ID));
    event_frame = find_tx(CAN_PROTOCOL_RTC_ALARM_EVENT_TX_ID, 1U);
    TEST_ASSERT_NOT_NULL(event_frame);
    TEST_ASSERT_EQUAL_UINT8(CAN_PROTOCOL_RTC_ALARM_EVENT_TRIGGERED,
                            event_frame->payload[0]);
    TEST_ASSERT_EQUAL_HEX8(
        CAN_PROTOCOL_RTC_ALARM_EVENT_AF |
        CAN_PROTOCOL_RTC_ALARM_EVENT_AIE |
        CAN_PROTOCOL_RTC_ALARM_EVENT_CONFIG_OK,
        event_frame->payload[1]);
    TEST_ASSERT_EQUAL_UINT8(read_datetime_value.hour,
                            event_frame->payload[2]);
    TEST_ASSERT_EQUAL(APP_LOG_EVENT_ALARM_TRIGGERED,
                      log_records[1].event);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_initialization_deadline_is_tick_wrap_safe);
    RUN_TEST(
        test_startup_discovers_watchdog_and_alarm_without_mutating_them);
    RUN_TEST(
        test_failed_init_retries_slowly_and_reports_reconnection_once);
    RUN_TEST(
        test_datetime_write_blocks_overlap_and_verifies_matching_readback);
    RUN_TEST(test_datetime_readback_mismatch_is_not_reported_as_success);
    RUN_TEST(test_reinitialization_cancels_pending_datetime_readback);
    RUN_TEST(
        test_datetime_driver_and_recovery_failures_are_both_reported);
    RUN_TEST(
        test_alarm_write_verification_handles_12_hour_readback_and_tick_wrap);
    RUN_TEST(test_alarm_readback_mismatch_is_latched_as_failure);
    RUN_TEST(
        test_alarm_event_retries_failed_transport_then_clears_flag);
    return UNITY_END();
}
