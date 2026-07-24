#include "main.h"
#include "rtc_app.h"
#include "pca2131.h"
#include "can_protocol.h"
#include "can_transport.h"
#include "app_log.h"

#define RTC_I2C_POLL_PERIOD_MS          100U
#define RTC_I2C_RETRY_PERIOD_MS         1000U
#define RTC_INIT_SETTLE_DELAY_MS        50U
#define RTC_WRITE_VERIFY_DELAY_MS       20U
#define RTC_ALARM_VERIFY_DELAY_MS       1U
#define RTC_ALARM_STATUS_POLL_PERIOD_MS 100U

extern I2C_HandleTypeDef hi2c1;

typedef struct
{
    uint8_t ready;
    uint8_t calendar_valid;

    uint8_t hundredth;
    uint8_t second;
    uint8_t minute;
    uint8_t hour;

    uint8_t day;
    uint8_t weekday;
    uint8_t month;
    uint8_t year;

    uint8_t osf;

    PCA2131_Result_t last_result;
    HAL_StatusTypeDef last_status;
    uint32_t last_error;

} PCA2131_RTC_t;

typedef enum
{
    RTC_WRITE_STATE_IDLE = 0,
    RTC_WRITE_STATE_WAIT_READBACK
} RTC_WriteState_t;

typedef enum
{
    RTC_INIT_STATE_NOT_STARTED = 0,
    RTC_INIT_STATE_WAIT_SETTLE,
    RTC_INIT_STATE_COMPLETE
} RTC_InitState_t;

typedef enum
{
    RTC_ALARM_WRITE_STATE_IDLE = 0,
    RTC_ALARM_WRITE_STATE_WAIT_READBACK
} RTC_AlarmWriteState_t;

typedef enum
{
    RTC_WRITE_LOG_STAGE_REJECTED_BUSY = 1U,
    RTC_WRITE_LOG_STAGE_DRIVER_WRITE,
    RTC_WRITE_LOG_STAGE_READBACK
} RTC_WriteLogStage_t;

typedef enum
{
    RTC_ALARM_LOG_STAGE_REJECTED_BUSY = 1U,
    RTC_ALARM_LOG_STAGE_NOT_READY,
    RTC_ALARM_LOG_STAGE_INVALID_CONFIG,
    RTC_ALARM_LOG_STAGE_PREWRITE_FLAG_CLEAR,
    RTC_ALARM_LOG_STAGE_DRIVER_WRITE,
    RTC_ALARM_LOG_STAGE_READBACK,
    RTC_ALARM_LOG_STAGE_VERIFY_MISMATCH,
    RTC_ALARM_LOG_STAGE_STATUS_READ,
    RTC_ALARM_LOG_STAGE_EVENT_TX,
    RTC_ALARM_LOG_STAGE_EVENT_FLAG_CLEAR
} RTC_AlarmLogStage_t;

static PCA2131_Device_t rtc_device;
static PCA2131_RTC_t rtc;
static RTC_InitState_t rtc_init_state = RTC_INIT_STATE_NOT_STARTED;
static uint32_t rtc_init_deadline = 0U;
static RTC_WriteState_t rtc_write_state = RTC_WRITE_STATE_IDLE;
static uint32_t rtc_write_verify_deadline = 0U;
static uint32_t last_rtc_poll_time = 0U;
static uint8_t last_published_rtc_second = 0xFFU;
static uint8_t rtc_force_publish = 1U;
static uint8_t last_published_rtc_hundredth = 0xFFU;
static RTC_AlarmSnapshot_t rtc_alarm_snapshot;
static uint8_t rtc_alarm_read_pending = 0U;
static RTC_AlarmWriteState_t rtc_alarm_write_state =
    RTC_ALARM_WRITE_STATE_IDLE;
static RTC_AlarmWriteStatus_t rtc_alarm_write_status;
static uint32_t rtc_alarm_verify_deadline = 0U;
static uint32_t last_alarm_status_poll_time = 0U;
static RTC_AlarmEventDiagnostics_t rtc_alarm_event_diagnostics;
static uint8_t rtc_alarm_runtime_failure_active = 0U;
static RTC_AlarmLogStage_t rtc_alarm_runtime_failure_stage =
    RTC_ALARM_LOG_STAGE_STATUS_READ;
static uint8_t rtc_watchdog_read_pending = 0U;

volatile RTC_WatchdogSnapshot_t g_rtcWatchdogSnapshot;

static uint32_t RTC_PackLogResult(PCA2131_Result_t driver_result,
                                  HAL_StatusTypeDef hal_status)
{
    return (((uint32_t)driver_result & 0x00FFFFFFUL) << 8U) |
           ((uint32_t)hal_status & 0xFFUL);
}

static void RTC_LogOperation(App_Log_Severity_t severity,
                             App_Log_EventCode_t event_code)
{
    (void)App_Log_Push(APP_LOG_SOURCE_RTC,
                       severity,
                       event_code,
                       RTC_PackLogResult(rtc.last_result,
                                         rtc.last_status),
                       rtc.last_error);
}

static uint32_t RTC_PackFailureDetail(uint8_t stage,
                                      uint32_t result,
                                      HAL_StatusTypeDef hal_status)
{
    return ((uint32_t)stage << 24U) |
           ((result & 0xFFFFUL) << 8U) |
           ((uint32_t)hal_status & 0xFFUL);
}

static void RTC_LogWriteFailure(RTC_WriteLogStage_t stage,
                                PCA2131_Result_t driver_result,
                                HAL_StatusTypeDef hal_status,
                                uint32_t hal_error)
{
    (void)App_Log_Push(APP_LOG_SOURCE_RTC,
                       APP_LOG_SEVERITY_FAULT,
                       APP_LOG_EVENT_RTC_WRITE_FAILED,
                       RTC_PackFailureDetail((uint8_t)stage,
                                             (uint32_t)driver_result,
                                             hal_status),
                       hal_error);
}

static void RTC_LogAlarmFailure(App_Log_Severity_t severity,
                                RTC_AlarmLogStage_t stage,
                                uint32_t result,
                                HAL_StatusTypeDef hal_status,
                                uint32_t hal_error)
{
    (void)App_Log_Push(APP_LOG_SOURCE_ALARM,
                       severity,
                       APP_LOG_EVENT_ALARM_FAILED,
                       RTC_PackFailureDetail((uint8_t)stage,
                                             result,
                                             hal_status),
                       hal_error);
}

static void RTC_LogAlarmRuntimeFailure(RTC_AlarmLogStage_t stage,
                                       uint32_t result,
                                       HAL_StatusTypeDef hal_status,
                                       uint32_t hal_error)
{
    if ((rtc_alarm_runtime_failure_active == 0U) ||
        (rtc_alarm_runtime_failure_stage != stage))
    {
        RTC_LogAlarmFailure(APP_LOG_SEVERITY_FAULT,
                            stage,
                            result,
                            hal_status,
                            hal_error);
        rtc_alarm_runtime_failure_active = 1U;
        rtc_alarm_runtime_failure_stage = stage;
    }
}

static void RTC_ClearAlarmRuntimeFailure(RTC_AlarmLogStage_t stage)
{
    if ((rtc_alarm_runtime_failure_active != 0U) &&
        (rtc_alarm_runtime_failure_stage == stage))
    {
        rtc_alarm_runtime_failure_active = 0U;
    }
}

static uint32_t RTC_PackAlarmEnableMask(
    const PCA2131_AlarmConfig_t *config)
{
    uint32_t enable_mask = 0U;

    enable_mask |= (config->second.enabled != 0U) ? (1UL << 0U) : 0U;
    enable_mask |= (config->minute.enabled != 0U) ? (1UL << 1U) : 0U;
    enable_mask |= (config->hour.enabled != 0U) ? (1UL << 2U) : 0U;
    enable_mask |= (config->day.enabled != 0U) ? (1UL << 3U) : 0U;
    enable_mask |= (config->weekday.enabled != 0U) ? (1UL << 4U) : 0U;
    return enable_mask;
}

static uint32_t RTC_PackAlarmValues(
    const PCA2131_AlarmConfig_t *config)
{
    /*
     * [5:0] second, [11:6] minute, [16:12] hour,
     * [21:17] day and [24:22] weekday.
     */
    return ((uint32_t)config->second.value & 0x3FUL) |
           (((uint32_t)config->minute.value & 0x3FUL) << 6U) |
           (((uint32_t)config->hour.value & 0x1FUL) << 12U) |
           (((uint32_t)config->day.value & 0x1FUL) << 17U) |
           (((uint32_t)config->weekday.value & 0x07UL) << 22U);
}

static void RTC_LogAlarmConfigured(
    const PCA2131_AlarmConfig_t *config)
{
    (void)App_Log_Push(APP_LOG_SOURCE_ALARM,
                       APP_LOG_SEVERITY_INFO,
                       APP_LOG_EVENT_ALARM_CONFIGURED,
                       RTC_PackAlarmEnableMask(config),
                       RTC_PackAlarmValues(config));
}

static void RTC_LogAlarmTriggered(
    const PCA2131_AlarmStatus_t *alarm_status)
{
    uint32_t flags = 0U;
    uint32_t packed_date;
    uint32_t packed_time;

    flags |= (alarm_status->alarm_flag != 0U) ? (1UL << 0U) : 0U;
    flags |= (alarm_status->interrupt_enabled != 0U) ? (1UL << 1U) : 0U;
    flags |= (rtc_alarm_snapshot.alarm.configuration_valid != 0U)
        ? (1UL << 2U)
        : 0U;

    /* data_0: [31:24] year, [23:16] month, [15:8] day, [7:0] flags. */
    packed_date = ((uint32_t)rtc.year << 24U) |
                  ((uint32_t)rtc.month << 16U) |
                  ((uint32_t)rtc.day << 8U) |
                  flags;
    /* data_1: [23:16] hour, [15:8] minute, [7:0] second. */
    packed_time = ((uint32_t)rtc.hour << 16U) |
                  ((uint32_t)rtc.minute << 8U) |
                  (uint32_t)rtc.second;

    (void)App_Log_Push(APP_LOG_SOURCE_ALARM,
                       APP_LOG_SEVERITY_INFO,
                       APP_LOG_EVENT_ALARM_TRIGGERED,
                       packed_date,
                       packed_time);
}

uint8_t PCA2131_Is_Valid_DateTime(uint8_t hundredth,
                                  uint8_t second,
                                  uint8_t minute,
                                  uint8_t hour,
                                  uint8_t day,
                                  uint8_t weekday,
                                  uint8_t month,
                                  uint8_t year)
{
    PCA2131_DateTime_t date_time = {0};

    date_time.hundredth = hundredth;
    date_time.second = second;
    date_time.minute = minute;
    date_time.hour = hour;
    date_time.day = day;
    date_time.weekday = weekday;
    date_time.month = month;
    date_time.year = year;

    return PCA2131_Driver_IsValidDateTime(&date_time);
}

void CAN_Send_RTC_Status(CAN_Protocol_RtcStatusCode_t status_code,
                         HAL_StatusTypeDef hal_status,
                         uint32_t hal_error)
{
    uint8_t txData[8] = {0};

    CAN_Protocol_EncodeRtcStatus(status_code,
                                 (uint8_t)hal_status,
                                 hal_error,
                                 txData);

    (void)CAN_Transport_SendClassicHighPriority(
        CAN_PROTOCOL_RTC_STATUS_TX_ID,
        CAN_TRANSPORT_ID_STANDARD,
        txData);
}

void CAN_Send_RTC_Time(void)
{
    CAN_Protocol_RtcTime_t rtc_time;
    uint8_t txData[8] = {0};

    /*
     RTC_TIME_TX_ID = 0x556

     Data[0] = Saat
     Data[1] = Dakika
     Data[2] = Saniye
     Data[3] = Saniyenin yüzde biri
     Data[4] = Ayın günü
     Data[5] = Ay
     Data[6] = Yıl (0-99)

     Data[7]:
     bit0-2 = Haftanın günü
     bit5   = Takvim geçerli
     bit6   = RTC/I2C hazır
     bit7   = OSF
    */

    rtc_time.hour = rtc.hour;
    rtc_time.minute = rtc.minute;
    rtc_time.second = rtc.second;
    rtc_time.hundredth = rtc.hundredth;
    rtc_time.day = rtc.day;
    rtc_time.month = rtc.month;
    rtc_time.year = rtc.year;
    rtc_time.weekday = rtc.weekday;
    rtc_time.calendar_valid = rtc.calendar_valid;
    rtc_time.ready = rtc.ready;
    rtc_time.osf = rtc.osf;

    CAN_Protocol_EncodeRtcTime(&rtc_time, txData);

    (void)CAN_Transport_SendClassicLatest(CAN_PROTOCOL_RTC_TIME_TX_ID,
                                          CAN_TRANSPORT_ID_STANDARD,
                                          txData);
}

void PCA2131_Init_Check(void)
{
    PCA2131_Driver_Init(&rtc_device, &hi2c1);

    rtc.ready = 0U;
    rtc_alarm_snapshot = (RTC_AlarmSnapshot_t){0};
    rtc_alarm_read_pending = 1U;
    rtc_alarm_write_state = RTC_ALARM_WRITE_STATE_IDLE;
    rtc_alarm_write_status = (RTC_AlarmWriteStatus_t){0};
    rtc_alarm_event_diagnostics = (RTC_AlarmEventDiagnostics_t){0};
    rtc_alarm_runtime_failure_active = 0U;
    rtc_alarm_runtime_failure_stage = RTC_ALARM_LOG_STAGE_STATUS_READ;
    g_rtcWatchdogSnapshot = (RTC_WatchdogSnapshot_t){0};
    rtc_watchdog_read_pending = 1U;
    last_alarm_status_poll_time = HAL_GetTick();
    rtc_init_deadline = HAL_GetTick() + RTC_INIT_SETTLE_DELAY_MS;
    rtc_init_state = RTC_INIT_STATE_WAIT_SETTLE;
}

uint8_t RTC_RefreshWatchdogSnapshot(void)
{
    PCA2131_WatchdogStatus_t watchdog_status = {0};
    PCA2131_OperationStatus_t operation_status;

    operation_status = PCA2131_Driver_ReadWatchdogStatus(
        &rtc_device,
        &watchdog_status);

    g_rtcWatchdogSnapshot.read_attempted = 1U;
    g_rtcWatchdogSnapshot.read_tick_ms = HAL_GetTick();
    g_rtcWatchdogSnapshot.driver_result = operation_status.result;
    g_rtcWatchdogSnapshot.hal_status = operation_status.hal_status;
    g_rtcWatchdogSnapshot.hal_error = operation_status.hal_error;
    g_rtcWatchdogSnapshot.status = watchdog_status;

    if (operation_status.result != PCA2131_RESULT_OK)
    {
        g_rtcWatchdogSnapshot.read_ok = 0U;
        return 0U;
    }

    g_rtcWatchdogSnapshot.read_ok = 1U;
    return 1U;
}

void RTC_GetWatchdogSnapshot(RTC_WatchdogSnapshot_t *snapshot)
{
    if (snapshot != NULL)
    {
        *snapshot = g_rtcWatchdogSnapshot;
    }
}

uint8_t RTC_RefreshAlarmSnapshot(void)
{
    PCA2131_Alarm_t alarm = {0};
    PCA2131_OperationStatus_t status;

    status = PCA2131_Driver_ReadAlarm(&rtc_device, &alarm);

    rtc_alarm_snapshot.read_attempted = 1U;
    rtc_alarm_snapshot.read_tick_ms = HAL_GetTick();
    rtc_alarm_snapshot.driver_result = status.result;
    rtc_alarm_snapshot.hal_status = status.hal_status;
    rtc_alarm_snapshot.hal_error = status.hal_error;
    rtc_alarm_snapshot.alarm = alarm;

    if (status.result != PCA2131_RESULT_OK)
    {
        rtc_alarm_snapshot.read_ok = 0U;
        return 0U;
    }

    rtc_alarm_snapshot.read_ok = 1U;
    return 1U;
}

void RTC_GetAlarmSnapshot(RTC_AlarmSnapshot_t *snapshot)
{
    if (snapshot != NULL)
    {
        *snapshot = rtc_alarm_snapshot;
    }
}

static uint8_t RTC_AlarmFieldMatches(
    const PCA2131_AlarmField_t *requested,
    const PCA2131_AlarmField_t *actual)
{
    if (requested->enabled != actual->enabled)
    {
        return 0U;
    }

    if (requested->enabled == 0U)
    {
        return 1U;
    }

    return (requested->value == actual->value) ? 1U : 0U;
}

static uint8_t RTC_AlarmHourMatches(
    const PCA2131_AlarmField_t *requested,
    const PCA2131_Alarm_t *actual)
{
    uint8_t actual_hour_24;

    if (requested->enabled != actual->hour.enabled)
    {
        return 0U;
    }

    if (requested->enabled == 0U)
    {
        return 1U;
    }

    if (actual->hour_mode_12 == 0U)
    {
        actual_hour_24 = actual->hour.value;
    }
    else
    {
        actual_hour_24 = actual->hour.value % 12U;

        if (actual->hour_pm != 0U)
        {
            actual_hour_24 += 12U;
        }
    }

    return (requested->value == actual_hour_24) ? 1U : 0U;
}

static uint8_t RTC_AlarmConfigurationMatches(
    const PCA2131_AlarmConfig_t *requested,
    const PCA2131_Alarm_t *actual)
{
    if (actual->configuration_valid == 0U)
    {
        return 0U;
    }

    if ((RTC_AlarmFieldMatches(&requested->second, &actual->second) == 0U) ||
        (RTC_AlarmFieldMatches(&requested->minute, &actual->minute) == 0U) ||
        (RTC_AlarmHourMatches(&requested->hour, actual) == 0U) ||
        (RTC_AlarmFieldMatches(&requested->day, &actual->day) == 0U) ||
        (RTC_AlarmFieldMatches(&requested->weekday,
                               &actual->weekday) == 0U))
    {
        return 0U;
    }

    return 1U;
}

uint8_t RTC_SetAlarm(const PCA2131_AlarmConfig_t *config)
{
    PCA2131_OperationStatus_t operation_status;

    rtc_alarm_write_status.request_count++;

    if ((rtc_init_state != RTC_INIT_STATE_COMPLETE) ||
        (rtc_write_state != RTC_WRITE_STATE_IDLE) ||
        (rtc_alarm_write_state != RTC_ALARM_WRITE_STATE_IDLE))
    {
        rtc_alarm_write_status.result = RTC_ALARM_WRITE_REJECTED_BUSY;
        rtc_alarm_write_status.completion_tick_ms = HAL_GetTick();
        CAN_Send_RTC_Status(CAN_PROTOCOL_RTC_STATUS_BUSY,
                            HAL_BUSY,
                            0U);
        RTC_LogAlarmFailure(
            APP_LOG_SEVERITY_WARNING,
            RTC_ALARM_LOG_STAGE_REJECTED_BUSY,
            (uint32_t)rtc_alarm_write_status.result,
            HAL_BUSY,
            0U);
        return 0U;
    }

    if (rtc.ready == 0U)
    {
        rtc_alarm_write_status.result = RTC_ALARM_WRITE_REJECTED_NOT_READY;
        rtc_alarm_write_status.completion_tick_ms = HAL_GetTick();
        CAN_Send_RTC_Status(CAN_PROTOCOL_RTC_STATUS_ALARM_NOT_READY,
                            rtc.last_status,
                            rtc.last_error);
        RTC_LogAlarmFailure(
            APP_LOG_SEVERITY_WARNING,
            RTC_ALARM_LOG_STAGE_NOT_READY,
            (uint32_t)rtc_alarm_write_status.result,
            rtc.last_status,
            rtc.last_error);
        return 0U;
    }

    if (PCA2131_Driver_IsValidAlarmConfig(config) == 0U)
    {
        rtc_alarm_write_status.result =
            RTC_ALARM_WRITE_REJECTED_INVALID_CONFIG;
        rtc_alarm_write_status.completion_tick_ms = HAL_GetTick();
        CAN_Send_RTC_Status(
            CAN_PROTOCOL_RTC_STATUS_INVALID_ALARM_CONFIG,
            HAL_ERROR,
            0U);
        RTC_LogAlarmFailure(
            APP_LOG_SEVERITY_WARNING,
            RTC_ALARM_LOG_STAGE_INVALID_CONFIG,
            (uint32_t)rtc_alarm_write_status.result,
            HAL_ERROR,
            0U);
        return 0U;
    }

    operation_status = PCA2131_Driver_ClearAlarmFlag(&rtc_device);
    rtc_alarm_write_status.driver_result = operation_status.result;
    rtc_alarm_write_status.hal_status = operation_status.hal_status;
    rtc_alarm_write_status.hal_error = operation_status.hal_error;

    if (operation_status.result != PCA2131_RESULT_OK)
    {
        rtc_alarm_write_status.result = RTC_ALARM_WRITE_FLAG_CLEAR_FAILED;
        rtc_alarm_write_status.failure_count++;
        rtc_alarm_write_status.completion_tick_ms = HAL_GetTick();
        CAN_Send_RTC_Status(
            CAN_PROTOCOL_RTC_STATUS_ALARM_FLAG_CLEAR_FAILED,
            operation_status.hal_status,
            operation_status.hal_error);
        RTC_LogAlarmFailure(
            APP_LOG_SEVERITY_FAULT,
            RTC_ALARM_LOG_STAGE_PREWRITE_FLAG_CLEAR,
            (uint32_t)operation_status.result,
            operation_status.hal_status,
            operation_status.hal_error);
        return 0U;
    }

    rtc_alarm_event_diagnostics.event_reported_for_active_flag = 0U;

    rtc_alarm_write_status.requested = *config;
    operation_status = PCA2131_Driver_WriteAlarm(&rtc_device, config);
    rtc_alarm_write_status.driver_result = operation_status.result;
    rtc_alarm_write_status.hal_status = operation_status.hal_status;
    rtc_alarm_write_status.hal_error = operation_status.hal_error;

    if (operation_status.result != PCA2131_RESULT_OK)
    {
        rtc_alarm_write_status.result = RTC_ALARM_WRITE_DRIVER_FAILED;
        rtc_alarm_write_status.failure_count++;
        rtc_alarm_write_status.completion_tick_ms = HAL_GetTick();
        CAN_Send_RTC_Status(CAN_PROTOCOL_RTC_STATUS_ALARM_WRITE_FAILED,
                            operation_status.hal_status,
                            operation_status.hal_error);
        RTC_LogAlarmFailure(
            APP_LOG_SEVERITY_FAULT,
            RTC_ALARM_LOG_STAGE_DRIVER_WRITE,
            (uint32_t)operation_status.result,
            operation_status.hal_status,
            operation_status.hal_error);
        return 0U;
    }

    rtc_alarm_write_status.result = RTC_ALARM_WRITE_PENDING;
    rtc_alarm_read_pending = 0U;
    rtc_alarm_verify_deadline = HAL_GetTick() + RTC_ALARM_VERIFY_DELAY_MS;
    rtc_alarm_write_state = RTC_ALARM_WRITE_STATE_WAIT_READBACK;

    return 1U;
}

void RTC_GetAlarmWriteStatus(RTC_AlarmWriteStatus_t *status)
{
    if (status != NULL)
    {
        *status = rtc_alarm_write_status;
    }
}

void RTC_GetAlarmEventDiagnostics(RTC_AlarmEventDiagnostics_t *diagnostics)
{
    if (diagnostics != NULL)
    {
        *diagnostics = rtc_alarm_event_diagnostics;
    }
}

static uint8_t RTC_ProcessPendingAlarmWrite(uint32_t now)
{
    if (rtc_alarm_write_state == RTC_ALARM_WRITE_STATE_IDLE)
    {
        return 0U;
    }

    if ((int32_t)(now - rtc_alarm_verify_deadline) < 0)
    {
        return 1U;
    }

    rtc_alarm_write_state = RTC_ALARM_WRITE_STATE_IDLE;

    if (RTC_RefreshAlarmSnapshot() == 0U)
    {
        rtc_alarm_write_status.result = RTC_ALARM_WRITE_READBACK_FAILED;
        rtc_alarm_write_status.driver_result =
            rtc_alarm_snapshot.driver_result;
        rtc_alarm_write_status.hal_status = rtc_alarm_snapshot.hal_status;
        rtc_alarm_write_status.hal_error = rtc_alarm_snapshot.hal_error;
        rtc_alarm_write_status.failure_count++;
        rtc_alarm_write_status.completion_tick_ms = now;
        CAN_Send_RTC_Status(
            CAN_PROTOCOL_RTC_STATUS_ALARM_READBACK_FAILED,
            rtc_alarm_snapshot.hal_status,
            rtc_alarm_snapshot.hal_error);
        RTC_LogAlarmFailure(
            APP_LOG_SEVERITY_FAULT,
            RTC_ALARM_LOG_STAGE_READBACK,
            (uint32_t)rtc_alarm_snapshot.driver_result,
            rtc_alarm_snapshot.hal_status,
            rtc_alarm_snapshot.hal_error);
        return 1U;
    }

    rtc_alarm_write_status.driver_result = rtc_alarm_snapshot.driver_result;
    rtc_alarm_write_status.hal_status = rtc_alarm_snapshot.hal_status;
    rtc_alarm_write_status.hal_error = rtc_alarm_snapshot.hal_error;
    rtc_alarm_write_status.completion_tick_ms = now;

    if (RTC_AlarmConfigurationMatches(
            &rtc_alarm_write_status.requested,
            &rtc_alarm_snapshot.alarm) == 0U)
    {
        rtc_alarm_write_status.result = RTC_ALARM_WRITE_VERIFY_MISMATCH;
        rtc_alarm_write_status.failure_count++;
        CAN_Send_RTC_Status(
            CAN_PROTOCOL_RTC_STATUS_ALARM_VERIFY_MISMATCH,
            rtc_alarm_snapshot.hal_status,
            rtc_alarm_snapshot.hal_error);
        RTC_LogAlarmFailure(
            APP_LOG_SEVERITY_FAULT,
            RTC_ALARM_LOG_STAGE_VERIFY_MISMATCH,
            (uint32_t)rtc_alarm_write_status.result,
            rtc_alarm_snapshot.hal_status,
            rtc_alarm_snapshot.hal_error);
        return 1U;
    }

    rtc_alarm_write_status.result = RTC_ALARM_WRITE_VERIFIED;
    rtc_alarm_write_status.verified_count++;
    CAN_Send_RTC_Status(
        CAN_PROTOCOL_RTC_STATUS_ALARM_WRITE_VERIFY_OK,
        rtc_alarm_snapshot.hal_status,
        rtc_alarm_snapshot.hal_error);
    RTC_LogAlarmConfigured(&rtc_alarm_write_status.requested);
    return 1U;
}

static uint8_t RTC_AlarmHasEnabledComparison(void)
{
    const PCA2131_Alarm_t *alarm = &rtc_alarm_snapshot.alarm;

    if ((rtc_alarm_snapshot.read_ok == 0U) ||
        (alarm->configuration_valid == 0U))
    {
        return 0U;
    }

    return ((alarm->second.enabled != 0U) ||
            (alarm->minute.enabled != 0U) ||
            (alarm->hour.enabled != 0U) ||
            (alarm->day.enabled != 0U) ||
            (alarm->weekday.enabled != 0U))
         ? 1U
         : 0U;
}

static CAN_Transport_Result_t RTC_SendAlarmEvent(
    const PCA2131_AlarmStatus_t *alarm_status)
{
    CAN_Protocol_RtcAlarmEvent_t event;
    uint8_t tx_data[CAN_PROTOCOL_PAYLOAD_SIZE] = {0};

    event.event_code = CAN_PROTOCOL_RTC_ALARM_EVENT_TRIGGERED;
    event.alarm_flag = alarm_status->alarm_flag;
    event.interrupt_enabled = alarm_status->interrupt_enabled;
    event.configuration_valid =
        rtc_alarm_snapshot.alarm.configuration_valid;
    event.hour = rtc.hour;
    event.minute = rtc.minute;
    event.second = rtc.second;
    event.day = rtc.day;
    event.month = rtc.month;
    event.year = rtc.year;

    CAN_Protocol_EncodeRtcAlarmEvent(&event, tx_data);

    return CAN_Transport_SendClassicHighPriority(
        CAN_PROTOCOL_RTC_ALARM_EVENT_TX_ID,
        CAN_TRANSPORT_ID_STANDARD,
        tx_data);
}

static void RTC_ProcessAlarmEvent(uint32_t now)
{
    PCA2131_AlarmStatus_t alarm_status;
    PCA2131_OperationStatus_t operation_status;
    CAN_Transport_Result_t tx_result;

    if ((rtc.ready == 0U) ||
        (RTC_AlarmHasEnabledComparison() == 0U) ||
        ((now - last_alarm_status_poll_time) <
         RTC_ALARM_STATUS_POLL_PERIOD_MS))
    {
        return;
    }

    last_alarm_status_poll_time = now;
    rtc_alarm_event_diagnostics.poll_count++;

    operation_status = PCA2131_Driver_ReadAlarmStatus(&rtc_device,
                                                       &alarm_status);
    rtc_alarm_event_diagnostics.driver_result = operation_status.result;
    rtc_alarm_event_diagnostics.hal_status = operation_status.hal_status;
    rtc_alarm_event_diagnostics.hal_error = operation_status.hal_error;

    if (operation_status.result != PCA2131_RESULT_OK)
    {
        rtc_alarm_event_diagnostics.status_read_failure_count++;
        RTC_LogAlarmRuntimeFailure(
            RTC_ALARM_LOG_STAGE_STATUS_READ,
            (uint32_t)operation_status.result,
            operation_status.hal_status,
            operation_status.hal_error);
        return;
    }

    RTC_ClearAlarmRuntimeFailure(RTC_ALARM_LOG_STAGE_STATUS_READ);

    rtc_alarm_event_diagnostics.last_alarm_flag =
        alarm_status.alarm_flag;
    rtc_alarm_event_diagnostics.last_interrupt_enabled =
        alarm_status.interrupt_enabled;

    if (alarm_status.alarm_flag == 0U)
    {
        rtc_alarm_event_diagnostics.event_reported_for_active_flag = 0U;
        rtc_alarm_runtime_failure_active = 0U;
        return;
    }

    if (rtc_alarm_event_diagnostics.event_reported_for_active_flag == 0U)
    {
        tx_result = RTC_SendAlarmEvent(&alarm_status);
        rtc_alarm_event_diagnostics.tx_attempted = 1U;
        rtc_alarm_event_diagnostics.last_tx_result = (uint8_t)tx_result;

        if ((tx_result != CAN_TRANSPORT_OK) &&
            (tx_result != CAN_TRANSPORT_QUEUED))
        {
            rtc_alarm_event_diagnostics.tx_failure_count++;
            RTC_LogAlarmRuntimeFailure(
                RTC_ALARM_LOG_STAGE_EVENT_TX,
                (uint32_t)tx_result,
                HAL_ERROR,
                0U);
            return;
        }

        RTC_ClearAlarmRuntimeFailure(RTC_ALARM_LOG_STAGE_EVENT_TX);
        rtc_alarm_event_diagnostics.event_count++;
        rtc_alarm_event_diagnostics.event_reported_for_active_flag = 1U;
        RTC_LogAlarmTriggered(&alarm_status);
    }

    operation_status = PCA2131_Driver_ClearAlarmFlag(&rtc_device);
    rtc_alarm_event_diagnostics.driver_result = operation_status.result;
    rtc_alarm_event_diagnostics.hal_status = operation_status.hal_status;
    rtc_alarm_event_diagnostics.hal_error = operation_status.hal_error;

    if (operation_status.result != PCA2131_RESULT_OK)
    {
        rtc_alarm_event_diagnostics.flag_clear_failure_count++;
        RTC_LogAlarmRuntimeFailure(
            RTC_ALARM_LOG_STAGE_EVENT_FLAG_CLEAR,
            (uint32_t)operation_status.result,
            operation_status.hal_status,
            operation_status.hal_error);
        return;
    }

    RTC_ClearAlarmRuntimeFailure(RTC_ALARM_LOG_STAGE_EVENT_FLAG_CLEAR);
    rtc_alarm_event_diagnostics.flag_clear_count++;
    rtc_alarm_event_diagnostics.last_alarm_flag = 0U;
    rtc_alarm_event_diagnostics.event_reported_for_active_flag = 0U;
}

static uint8_t RTC_ProcessInitialization(uint32_t now)
{
    PCA2131_OperationStatus_t status;

    if (rtc_init_state == RTC_INIT_STATE_COMPLETE)
    {
        return 0U;
    }

    if (rtc_init_state == RTC_INIT_STATE_NOT_STARTED)
    {
        return 1U;
    }

    if ((int32_t)(now - rtc_init_deadline) < 0)
    {
        return 1U;
    }

    status = PCA2131_Driver_CheckReady(&rtc_device);

    rtc.last_status = status.hal_status;
    rtc.last_error = status.hal_error;
    rtc.last_result = status.result;
    rtc_init_state = RTC_INIT_STATE_COMPLETE;

    if (status.result == PCA2131_RESULT_OK)
    {
        rtc.ready = 1U;
        CAN_Send_RTC_Status(CAN_PROTOCOL_RTC_STATUS_INIT_OK,
                            status.hal_status,
                            status.hal_error);
        RTC_LogOperation(APP_LOG_SEVERITY_INFO,
                         APP_LOG_EVENT_RTC_INIT_OK);
    }
    else
    {
        rtc.ready = 0U;
        CAN_Send_RTC_Status(CAN_PROTOCOL_RTC_STATUS_INIT_FAILED,
                            status.hal_status,
                            status.hal_error);
        RTC_LogOperation(APP_LOG_SEVERITY_FAULT,
                         APP_LOG_EVENT_RTC_INIT_FAILED);
    }

    return 1U;
}

uint8_t PCA2131_UpdateTime(void)
{
    PCA2131_DateTime_t date_time;
    PCA2131_OperationStatus_t status;

    status = PCA2131_Driver_ReadDateTime(&rtc_device, &date_time);

    rtc.last_status = status.hal_status;
    rtc.last_error = status.hal_error;
    rtc.last_result = status.result;

    if (status.result != PCA2131_RESULT_OK)
    {
        rtc.ready = 0U;
        return 0U;
    }

    rtc.ready = 1U;
    rtc.hundredth = date_time.hundredth;
    rtc.second = date_time.second;
    rtc.minute = date_time.minute;
    rtc.hour = date_time.hour;
    rtc.day = date_time.day;
    rtc.weekday = date_time.weekday;
    rtc.month = date_time.month;
    rtc.year = date_time.year;
    rtc.osf = date_time.osf;
    rtc.calendar_valid = date_time.calendar_valid;

    return 1U;
}

static CAN_Protocol_RtcStatusCode_t PCA2131_GetWriteErrorStatusCode(
    PCA2131_Result_t result)
{
    switch (result)
    {
        case PCA2131_RESULT_CONTROL_READ_FAILED:
            return CAN_PROTOCOL_RTC_STATUS_CONTROL_READ_FAILED;

        case PCA2131_RESULT_STOP_WRITE_FAILED:
            return CAN_PROTOCOL_RTC_STATUS_STOP_WRITE_FAILED;

        case PCA2131_RESULT_CPR_WRITE_FAILED:
            return CAN_PROTOCOL_RTC_STATUS_CPR_WRITE_FAILED;

        case PCA2131_RESULT_CALENDAR_WRITE_FAILED:
            return CAN_PROTOCOL_RTC_STATUS_CALENDAR_WRITE_FAILED;

        case PCA2131_RESULT_START_WRITE_FAILED:
            return CAN_PROTOCOL_RTC_STATUS_START_WRITE_FAILED;

        case PCA2131_RESULT_INVALID_ARGUMENT:
        case PCA2131_RESULT_INVALID_DATETIME:
        default:
            return CAN_PROTOCOL_RTC_STATUS_INVALID_DATETIME;
    }
}

static void PCA2131_ReportWriteFailure(
    const PCA2131_OperationStatus_t *status)
{
    CAN_Protocol_RtcStatusCode_t status_code;

    status_code = PCA2131_GetWriteErrorStatusCode(status->result);

    if ((status->result != PCA2131_RESULT_INVALID_ARGUMENT) &&
        (status->result != PCA2131_RESULT_INVALID_DATETIME))
    {
        rtc.ready = 0U;
    }

    rtc.last_status = status->hal_status;
    rtc.last_error = status->hal_error;
    rtc.last_result = status->result;

    CAN_Send_RTC_Status(status_code,
                        status->hal_status,
                        status->hal_error);

    if ((status->recovery_attempted != 0U) &&
        (status->recovery_status != HAL_OK))
    {
        rtc.last_status = status->recovery_status;
        rtc.last_error = status->recovery_error;

        CAN_Send_RTC_Status(CAN_PROTOCOL_RTC_STATUS_RECOVERY_FAILED,
                            status->recovery_status,
                            status->recovery_error);
    }
}

uint8_t PCA2131_SetDateTime(uint8_t hundredth,
                            uint8_t second,
                            uint8_t minute,
                            uint8_t hour,
                            uint8_t day,
                            uint8_t weekday,
                            uint8_t month,
                            uint8_t year)
{
    PCA2131_DateTime_t date_time = {0};
    PCA2131_OperationStatus_t status;

    if (rtc_init_state != RTC_INIT_STATE_COMPLETE)
    {
        CAN_Send_RTC_Status(CAN_PROTOCOL_RTC_STATUS_BUSY,
                            HAL_BUSY,
                            0U);
        RTC_LogWriteFailure(RTC_WRITE_LOG_STAGE_REJECTED_BUSY,
                            PCA2131_RESULT_OK,
                            HAL_BUSY,
                            0U);
        return 0U;
    }

    if (rtc_write_state != RTC_WRITE_STATE_IDLE)
    {
        CAN_Send_RTC_Status(CAN_PROTOCOL_RTC_STATUS_BUSY,
                            HAL_BUSY,
                            0U);
        RTC_LogWriteFailure(RTC_WRITE_LOG_STAGE_REJECTED_BUSY,
                            PCA2131_RESULT_OK,
                            HAL_BUSY,
                            0U);
        return 0U;
    }

    if (rtc_alarm_write_state != RTC_ALARM_WRITE_STATE_IDLE)
    {
        CAN_Send_RTC_Status(CAN_PROTOCOL_RTC_STATUS_BUSY,
                            HAL_BUSY,
                            0U);
        RTC_LogWriteFailure(RTC_WRITE_LOG_STAGE_REJECTED_BUSY,
                            PCA2131_RESULT_OK,
                            HAL_BUSY,
                            0U);
        return 0U;
    }

    date_time.hundredth = hundredth;
    date_time.second = second;
    date_time.minute = minute;
    date_time.hour = hour;
    date_time.day = day;
    date_time.weekday = weekday;
    date_time.month = month;
    date_time.year = year;

    status = PCA2131_Driver_WriteDateTime(&rtc_device, &date_time);

    if (status.result != PCA2131_RESULT_OK)
    {
        PCA2131_ReportWriteFailure(&status);
        RTC_LogWriteFailure(RTC_WRITE_LOG_STAGE_DRIVER_WRITE,
                            status.result,
                            rtc.last_status,
                            rtc.last_error);
        return 0U;
    }

    rtc.last_result = status.result;
    rtc.last_status = status.hal_status;
    rtc.last_error = status.hal_error;
    rtc.ready = 1U;
    rtc.hour = hour;
    rtc.minute = minute;
    rtc.second = second;
    rtc.hundredth = hundredth;
    rtc.day = day;
    rtc.weekday = weekday;
    rtc.month = month;
    rtc.year = year;
    rtc.osf = 0U;
    rtc.calendar_valid = 1U;

    rtc_write_verify_deadline = HAL_GetTick() + RTC_WRITE_VERIFY_DELAY_MS;
    rtc_write_state = RTC_WRITE_STATE_WAIT_READBACK;

    return 1U;
}

static uint8_t RTC_ProcessPendingWrite(uint32_t now)
{
    if (rtc_write_state == RTC_WRITE_STATE_IDLE)
    {
        return 0U;
    }

    if ((int32_t)(now - rtc_write_verify_deadline) < 0)
    {
        return 1U;
    }

    rtc_write_state = RTC_WRITE_STATE_IDLE;

    if (PCA2131_UpdateTime() == 0U)
    {
        CAN_Send_RTC_Status(CAN_PROTOCOL_RTC_STATUS_READ_FAILED,
                            rtc.last_status,
                            rtc.last_error);
        RTC_LogWriteFailure(RTC_WRITE_LOG_STAGE_READBACK,
                            rtc.last_result,
                            rtc.last_status,
                            rtc.last_error);
        CAN_Send_RTC_Time();
        return 1U;
    }

    CAN_Send_RTC_Status(CAN_PROTOCOL_RTC_STATUS_WRITE_VERIFY_OK,
                        rtc.last_status,
                        rtc.last_error);
    RTC_LogOperation(APP_LOG_SEVERITY_INFO,
                     APP_LOG_EVENT_RTC_WRITE_OK);

    last_published_rtc_second = rtc.second;
    last_published_rtc_hundredth = rtc.hundredth;
    rtc_force_publish = 0U;

    CAN_Send_RTC_Time();

    return 1U;
}

uint8_t PCA2131_SetTime(uint8_t hour, uint8_t minute, uint8_t second)
{
    /*
     Eski 0x20 komutuyla uyumluluk korunur. Bu komut yalnızca zamanı
     değiştirir; mevcut tarih alanları yeniden yazılır ve yüzde birlik
     saniye sıfırlanır.
    */
    return PCA2131_SetDateTime(0U,
                               second,
                               minute,
                               hour,
                               rtc.day,
                               rtc.weekday,
                               rtc.month,
                               rtc.year);
}

void CAN_Handle_RTC_Set_Time(uint8_t *data)
{
    uint8_t hour;
    uint8_t minute;
    uint8_t second;

    hour = data[1];
    minute = data[2];
    second = data[3];

    PCA2131_SetTime(hour, minute, second);
}

void CAN_Handle_RTC_Set_DateTime(uint8_t *data)
{
    uint8_t month;
    uint8_t weekday;

    month = data[6] & 0x1FU;
    weekday = (data[6] >> 5) & 0x07U;

    PCA2131_SetDateTime(data[1],
                        data[2],
                        data[3],
                        data[4],
                        data[5],
                        weekday,
                        month,
                        data[7]);
}

void CAN_Handle_RTC_Set_Alarm(uint8_t *data)
{
    CAN_Protocol_RtcAlarmCommand_t command;
    PCA2131_AlarmConfig_t config = {0};

    if (CAN_Protocol_DecodeRtcAlarmCommand(data, &command) == 0U)
    {
        return;
    }

    config.second.enabled =
        ((command.enable_mask & CAN_PROTOCOL_RTC_ALARM_ENABLE_SECOND) != 0U)
        ? 1U
        : 0U;
    config.second.value = command.second;

    config.minute.enabled =
        ((command.enable_mask & CAN_PROTOCOL_RTC_ALARM_ENABLE_MINUTE) != 0U)
        ? 1U
        : 0U;
    config.minute.value = command.minute;

    config.hour.enabled =
        ((command.enable_mask & CAN_PROTOCOL_RTC_ALARM_ENABLE_HOUR) != 0U)
        ? 1U
        : 0U;
    config.hour.value = command.hour;

    config.day.enabled =
        ((command.enable_mask & CAN_PROTOCOL_RTC_ALARM_ENABLE_DAY) != 0U)
        ? 1U
        : 0U;
    config.day.value = command.day;

    config.weekday.enabled =
        ((command.enable_mask & CAN_PROTOCOL_RTC_ALARM_ENABLE_WEEKDAY) != 0U)
        ? 1U
        : 0U;
    config.weekday.value = command.weekday;

    (void)RTC_SetAlarm(&config);
}

void RTC_CAN_Publish_Process(void)
{
    uint32_t now = HAL_GetTick();

    if (rtc_write_state != RTC_WRITE_STATE_IDLE)
    {
        return;
    }

    if ((now - last_rtc_poll_time) >= RTC_I2C_POLL_PERIOD_MS)
    {
    	last_rtc_poll_time = now;

        /*
         RTC değeri I2C ile güncellendiyse CAN üzerinden yayınla.
         CAN yoksa bu gönderim başarısız olabilir ama I2C tarafını bozmaz.
        */
        if (rtc.ready != 0U)
        {
            CAN_Send_RTC_Time();
        }
        else
        {
            CAN_Send_RTC_Status(CAN_PROTOCOL_RTC_STATUS_READ_FAILED,
                                rtc.last_status,
                                rtc.last_error);
        }
    }
}

void RTC_Process(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t poll_period_ms;
    uint8_t was_ready;

    if (RTC_ProcessInitialization(now) != 0U)
    {
        return;
    }

    if (RTC_ProcessPendingWrite(now) != 0U)
    {
        return;
    }

    if (RTC_ProcessPendingAlarmWrite(now) != 0U)
    {
        return;
    }

    /* Read-only discovery step: do not enable or reload the watchdog yet. */
    if ((rtc_watchdog_read_pending != 0U) && (rtc.ready != 0U))
    {
        rtc_watchdog_read_pending = 0U;
        (void)RTC_RefreshWatchdogSnapshot();
        return;
    }

    /*
     Alarm registerleri açılışta yalnızca bir kez okunur. Alarm okuma
     hatası, takvim/saat okumasının çalışmasını engellemez.
    */
    if ((rtc_alarm_read_pending != 0U) && (rtc.ready != 0U))
    {
        rtc_alarm_read_pending = 0U;
        (void)RTC_RefreshAlarmSnapshot();
        return;
    }

    /*
     RTC sağlıklıysa 100 ms'de bir oku.
     RTC hatalıysa yalnızca 1000 ms'de bir tekrar dene.
    */
    if (rtc.ready != 0U)
    {
        poll_period_ms = RTC_I2C_POLL_PERIOD_MS;
    }
    else
    {
        poll_period_ms = RTC_I2C_RETRY_PERIOD_MS;
    }

    if ((now - last_rtc_poll_time) >= poll_period_ms)
    {
        last_rtc_poll_time = now;

        /*
         Okuma işleminden önceki bağlantı durumunu sakla.
         PCA2131_UpdateTime() rtc.ready değerini değiştirebilir.
        */
        was_ready = rtc.ready;

        if (PCA2131_UpdateTime() != 0U)
        {
            /*
             RTC daha önce hatalıydı ve şimdi tekrar cevap verdi.
             A3 = RTC bağlantısı yeniden kuruldu.
            */
            if (was_ready == 0U)
            {
                CAN_Send_RTC_Status(
                    CAN_PROTOCOL_RTC_STATUS_RECONNECTED,
                    rtc.last_status,
                    rtc.last_error
                );
                RTC_LogOperation(APP_LOG_SEVERITY_INFO,
                                 APP_LOG_EVENT_RTC_RECOVERED);

                /*
                 Bağlantı geri geldiğinde saat mesajını
                 beklemeden yeniden yayınla.
                */
                rtc_force_publish = 1U;
            }

            if ((rtc_force_publish != 0U) ||
                (rtc.second != last_published_rtc_second) ||
                (rtc.hundredth != last_published_rtc_hundredth))
            {
            	rtc_force_publish = 0U;

            	last_published_rtc_second = rtc.second;
            	last_published_rtc_hundredth = rtc.hundredth;

            	CAN_Send_RTC_Time();
            }
        }
        else
        {
            /*
             RTC önceki okumada çalışıyordu fakat şimdi hata verdi.
             Yalnızca ilk geçişte E2 gönderilir; her saniye tekrarlanmaz.
            */
            if (was_ready != 0U)
            {
                rtc_force_publish = 1U;

                CAN_Send_RTC_Status(
                    CAN_PROTOCOL_RTC_STATUS_READ_FAILED,
                    rtc.last_status,
                    rtc.last_error
                );
                RTC_LogOperation(APP_LOG_SEVERITY_FAULT,
                                 APP_LOG_EVENT_RTC_READ_FAILED);
            }
        }
    }

    RTC_ProcessAlarmEvent(now);
}
