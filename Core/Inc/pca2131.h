#ifndef PCA2131_H
#define PCA2131_H

#include "stm32h7xx_hal.h"

typedef enum
{
    PCA2131_RESULT_OK = 0,
    PCA2131_RESULT_INVALID_ARGUMENT,
    PCA2131_RESULT_INVALID_DATETIME,
    PCA2131_RESULT_DEVICE_NOT_READY,
    PCA2131_RESULT_READ_FAILED,
    PCA2131_RESULT_CONTROL_READ_FAILED,
    PCA2131_RESULT_STOP_WRITE_FAILED,
    PCA2131_RESULT_CPR_WRITE_FAILED,
    PCA2131_RESULT_CALENDAR_WRITE_FAILED,
    PCA2131_RESULT_START_WRITE_FAILED,
    PCA2131_RESULT_ALARM_READ_FAILED,
    PCA2131_RESULT_INVALID_ALARM_CONFIG,
    PCA2131_RESULT_ALARM_CONTROL_READ_FAILED,
    PCA2131_RESULT_ALARM_WRITE_FAILED,
    PCA2131_RESULT_ALARM_STATUS_READ_FAILED,
    PCA2131_RESULT_ALARM_INTERRUPT_WRITE_FAILED,
    PCA2131_RESULT_ALARM_FLAG_CLEAR_FAILED,
    PCA2131_RESULT_WATCHDOG_STATUS_READ_FAILED
} PCA2131_Result_t;

typedef struct
{
    uint8_t hundredth;
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t weekday;
    uint8_t month;
    uint8_t year;
    uint8_t osf;
    uint8_t calendar_valid;
} PCA2131_DateTime_t;

typedef struct
{
    PCA2131_Result_t result;
    HAL_StatusTypeDef hal_status;
    uint32_t hal_error;
    uint8_t recovery_attempted;
    HAL_StatusTypeDef recovery_status;
    uint32_t recovery_error;
} PCA2131_OperationStatus_t;

typedef struct
{
    I2C_HandleTypeDef *i2c;
} PCA2131_Device_t;

typedef struct
{
    uint8_t enabled;
    uint8_t value;
} PCA2131_AlarmField_t;

typedef struct
{
    PCA2131_AlarmField_t second;
    PCA2131_AlarmField_t minute;
    PCA2131_AlarmField_t hour;
    PCA2131_AlarmField_t day;
    PCA2131_AlarmField_t weekday;
    uint8_t hour_mode_12;
    uint8_t hour_pm;
    uint8_t alarm_flag;
    uint8_t interrupt_enabled;
    uint8_t configuration_valid;
} PCA2131_Alarm_t;

/*
 * Alarm hour is always expressed as 0..23 at the API boundary. The driver
 * converts it to the RTC's active 12-hour or 24-hour register format.
 */
typedef struct
{
    PCA2131_AlarmField_t second;
    PCA2131_AlarmField_t minute;
    PCA2131_AlarmField_t hour;
    PCA2131_AlarmField_t day;
    PCA2131_AlarmField_t weekday;
} PCA2131_AlarmConfig_t;

typedef struct
{
    uint8_t alarm_flag;
    uint8_t interrupt_enabled;
} PCA2131_AlarmStatus_t;

typedef enum
{
    PCA2131_WATCHDOG_CLOCK_64_HZ = 0U,
    PCA2131_WATCHDOG_CLOCK_4_HZ,
    PCA2131_WATCHDOG_CLOCK_1_DIV_4_HZ,
    PCA2131_WATCHDOG_CLOCK_1_DIV_64_HZ
} PCA2131_WatchdogClock_t;

typedef struct
{
    uint8_t enabled;
    uint8_t timeout_flag;
    uint8_t interrupt_a_masked;
    uint8_t interrupt_b_masked;
    PCA2131_WatchdogClock_t clock_source;
    uint8_t raw_control_2;
    uint8_t raw_int_a_mask1;
    uint8_t raw_int_b_mask1;
    uint8_t raw_watchdog_control;
} PCA2131_WatchdogStatus_t;

void PCA2131_Driver_Init(PCA2131_Device_t *device,
                         I2C_HandleTypeDef *i2c);

uint8_t PCA2131_Driver_IsValidDateTime(
    const PCA2131_DateTime_t *date_time);

PCA2131_OperationStatus_t PCA2131_Driver_CheckReady(
    const PCA2131_Device_t *device);

PCA2131_OperationStatus_t PCA2131_Driver_ReadDateTime(
    const PCA2131_Device_t *device,
    PCA2131_DateTime_t *date_time);

PCA2131_OperationStatus_t PCA2131_Driver_ReadAlarm(
    const PCA2131_Device_t *device,
    PCA2131_Alarm_t *alarm);

uint8_t PCA2131_Driver_IsValidAlarmConfig(
    const PCA2131_AlarmConfig_t *config);

PCA2131_OperationStatus_t PCA2131_Driver_WriteAlarm(
    const PCA2131_Device_t *device,
    const PCA2131_AlarmConfig_t *config);

PCA2131_OperationStatus_t PCA2131_Driver_ReadAlarmStatus(
    const PCA2131_Device_t *device,
    PCA2131_AlarmStatus_t *status);

PCA2131_OperationStatus_t PCA2131_Driver_SetAlarmInterruptEnabled(
    const PCA2131_Device_t *device,
    uint8_t enabled);

PCA2131_OperationStatus_t PCA2131_Driver_ClearAlarmFlag(
    const PCA2131_Device_t *device);

PCA2131_OperationStatus_t PCA2131_Driver_ReadWatchdogStatus(
    const PCA2131_Device_t *device,
    PCA2131_WatchdogStatus_t *status);

PCA2131_OperationStatus_t PCA2131_Driver_WriteDateTime(
    const PCA2131_Device_t *device,
    const PCA2131_DateTime_t *date_time);

#endif /* PCA2131_H */
