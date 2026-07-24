#ifndef RTC_APP_H
#define RTC_APP_H

#include "main.h"
#include "can_protocol.h"
#include "pca2131.h"

typedef struct
{
    uint8_t read_attempted;
    uint8_t read_ok;
    uint32_t read_tick_ms;
    PCA2131_Result_t driver_result;
    HAL_StatusTypeDef hal_status;
    uint32_t hal_error;
    PCA2131_Alarm_t alarm;
} RTC_AlarmSnapshot_t;

typedef enum
{
    RTC_ALARM_WRITE_NOT_ATTEMPTED = 0,
    RTC_ALARM_WRITE_PENDING,
    RTC_ALARM_WRITE_VERIFIED,
    RTC_ALARM_WRITE_REJECTED_BUSY,
    RTC_ALARM_WRITE_REJECTED_NOT_READY,
    RTC_ALARM_WRITE_REJECTED_INVALID_CONFIG,
    RTC_ALARM_WRITE_FLAG_CLEAR_FAILED,
    RTC_ALARM_WRITE_DRIVER_FAILED,
    RTC_ALARM_WRITE_READBACK_FAILED,
    RTC_ALARM_WRITE_VERIFY_MISMATCH
} RTC_AlarmWriteResult_t;

typedef struct
{
    RTC_AlarmWriteResult_t result;
    uint32_t request_count;
    uint32_t verified_count;
    uint32_t failure_count;
    uint32_t completion_tick_ms;
    PCA2131_Result_t driver_result;
    HAL_StatusTypeDef hal_status;
    uint32_t hal_error;
    PCA2131_AlarmConfig_t requested;
} RTC_AlarmWriteStatus_t;

typedef struct
{
    uint32_t poll_count;
    uint32_t event_count;
    uint32_t status_read_failure_count;
    uint32_t tx_failure_count;
    uint32_t flag_clear_count;
    uint32_t flag_clear_failure_count;
    uint8_t last_alarm_flag;
    uint8_t last_interrupt_enabled;
    uint8_t event_reported_for_active_flag;
    uint8_t tx_attempted;
    uint8_t last_tx_result;
    PCA2131_Result_t driver_result;
    HAL_StatusTypeDef hal_status;
    uint32_t hal_error;
} RTC_AlarmEventDiagnostics_t;

typedef struct
{
    uint8_t read_attempted;
    uint8_t read_ok;
    uint32_t read_tick_ms;
    PCA2131_Result_t driver_result;
    HAL_StatusTypeDef hal_status;
    uint32_t hal_error;
    PCA2131_WatchdogStatus_t status;
} RTC_WatchdogSnapshot_t;

/* Exposed for debugger inspection. Application code must use the API. */
extern volatile RTC_WatchdogSnapshot_t g_rtcWatchdogSnapshot;

uint8_t PCA2131_Is_Valid_DateTime(uint8_t hundredth,
                                  uint8_t second,
                                  uint8_t minute,
                                  uint8_t hour,
                                  uint8_t day,
                                  uint8_t weekday,
                                  uint8_t month,
                                  uint8_t year);

void PCA2131_Init_Check(void);
uint8_t PCA2131_UpdateTime(void);
uint8_t PCA2131_SetTime(uint8_t hour, uint8_t minute, uint8_t second);
uint8_t PCA2131_SetDateTime(uint8_t hundredth,
                            uint8_t second,
                            uint8_t minute,
                            uint8_t hour,
                            uint8_t day,
                            uint8_t weekday,
                            uint8_t month,
                            uint8_t year);

void CAN_Handle_RTC_Set_Time(uint8_t *data);
void CAN_Handle_RTC_Set_DateTime(uint8_t *data);
void CAN_Handle_RTC_Set_Alarm(uint8_t *data);
void CAN_Send_RTC_Status(CAN_Protocol_RtcStatusCode_t status_code,
                         HAL_StatusTypeDef hal_status,
                         uint32_t hal_error);
void CAN_Send_RTC_Time(void);

void RTC_CAN_Publish_Process(void);
void RTC_Process(void);

uint8_t RTC_RefreshAlarmSnapshot(void);
void RTC_GetAlarmSnapshot(RTC_AlarmSnapshot_t *snapshot);
uint8_t RTC_SetAlarm(const PCA2131_AlarmConfig_t *config);
void RTC_GetAlarmWriteStatus(RTC_AlarmWriteStatus_t *status);
void RTC_GetAlarmEventDiagnostics(RTC_AlarmEventDiagnostics_t *diagnostics);
uint8_t RTC_RefreshWatchdogSnapshot(void);
void RTC_GetWatchdogSnapshot(RTC_WatchdogSnapshot_t *snapshot);

#endif /* RTC_APP_H */
