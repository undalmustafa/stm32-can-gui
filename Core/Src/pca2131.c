#include "pca2131.h"

#define PCA2131_I2C_ADDR                (0x53U << 1)
#define PCA2131_REG_CONTROL_1           0x00U
#define PCA2131_REG_CONTROL_2           0x01U
#define PCA2131_REG_SECOND_ALARM        0x0EU
#define PCA2131_REG_SR_RESET            0x05U
#define PCA2131_REG_100TH_SECONDS       0x06U
#define PCA2131_REG_INT_A_MASK1         0x31U
#define PCA2131_REG_WATCHDOG_CONTROL    0x35U
#define PCA2131_CONTROL1_STOP_BIT       0x20U
#define PCA2131_CONTROL1_100TH_S_DIS    0x10U
#define PCA2131_CONTROL1_12_24_BIT      0x04U
#define PCA2131_CONTROL2_AF_BIT         0x10U
#define PCA2131_CONTROL2_AIE_BIT        0x02U
#define PCA2131_CONTROL2_MSF_BIT        0x80U
#define PCA2131_CONTROL2_WDTF_BIT       0x40U
#define PCA2131_WATCHDOG_ENABLE_BIT     0x80U
#define PCA2131_WATCHDOG_MASK_BIT       0x08U
#define PCA2131_WATCHDOG_CLOCK_MASK     0x03U
#define PCA2131_ALARM_DISABLE_BIT       0x80U
#define PCA2131_ALARM_PM_BIT            0x20U
#define PCA2131_SR_RESET_CPR            0xA4U
#define PCA2131_I2C_TIMEOUT_MS          10U

static uint8_t PCA2131_BcdToDec(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) * 10U) + (bcd & 0x0FU));
}

static uint8_t PCA2131_DecToBcd(uint8_t decimal)
{
    return (uint8_t)(((decimal / 10U) << 4) | (decimal % 10U));
}

static uint8_t PCA2131_IsValidBcd(uint8_t bcd)
{
    return ((((bcd >> 4) & 0x0FU) <= 9U) &&
            ((bcd & 0x0FU) <= 9U)) ? 1U : 0U;
}

static uint8_t PCA2131_DecodeAlarmBcdField(
    uint8_t raw_value,
    uint8_t value_mask,
    uint8_t minimum,
    uint8_t maximum,
    PCA2131_AlarmField_t *field)
{
    uint8_t bcd_value = raw_value & value_mask;

    field->enabled = ((raw_value & PCA2131_ALARM_DISABLE_BIT) == 0U)
                   ? 1U
                   : 0U;

    if (PCA2131_IsValidBcd(bcd_value) == 0U)
    {
        field->value = 0U;
        return (field->enabled == 0U) ? 1U : 0U;
    }

    field->value = PCA2131_BcdToDec(bcd_value);

    if (field->enabled == 0U)
    {
        return 1U;
    }

    return ((field->value >= minimum) && (field->value <= maximum))
         ? 1U
         : 0U;
}

static uint8_t PCA2131_DecodeAlarmWeekday(
    uint8_t raw_value,
    PCA2131_AlarmField_t *field)
{
    field->enabled = ((raw_value & PCA2131_ALARM_DISABLE_BIT) == 0U)
                   ? 1U
                   : 0U;
    field->value = raw_value & 0x07U;

    if (field->enabled == 0U)
    {
        return 1U;
    }

    return (field->value <= 6U) ? 1U : 0U;
}

static uint8_t PCA2131_IsValidAlarmField(
    const PCA2131_AlarmField_t *field,
    uint8_t minimum,
    uint8_t maximum)
{
    if (field->enabled > 1U)
    {
        return 0U;
    }

    if (field->enabled == 0U)
    {
        return 1U;
    }

    return ((field->value >= minimum) && (field->value <= maximum))
         ? 1U
         : 0U;
}

static uint8_t PCA2131_EncodeAlarmField(
    const PCA2131_AlarmField_t *field,
    uint8_t value_mask)
{
    if (field->enabled == 0U)
    {
        return PCA2131_ALARM_DISABLE_BIT;
    }

    return PCA2131_DecToBcd(field->value) & value_mask;
}

static uint8_t PCA2131_EncodeAlarmHour(
    const PCA2131_AlarmField_t *field,
    uint8_t hour_mode_12)
{
    uint8_t hour_12;
    uint8_t pm_bit = 0U;

    if (field->enabled == 0U)
    {
        return PCA2131_ALARM_DISABLE_BIT;
    }

    if (hour_mode_12 == 0U)
    {
        return PCA2131_DecToBcd(field->value) & 0x3FU;
    }

    if (field->value == 0U)
    {
        hour_12 = 12U;
    }
    else if (field->value < 12U)
    {
        hour_12 = field->value;
    }
    else if (field->value == 12U)
    {
        hour_12 = 12U;
        pm_bit = PCA2131_ALARM_PM_BIT;
    }
    else
    {
        hour_12 = field->value - 12U;
        pm_bit = PCA2131_ALARM_PM_BIT;
    }

    return (PCA2131_DecToBcd(hour_12) & 0x1FU) | pm_bit;
}

static uint8_t PCA2131_IsLeapYear(uint8_t year)
{
    /*
     * PCA2131 stores an offset in the range 00..99, representing
     * 2000..2099. Year 00 is therefore 2000 and is a leap year.
     */
    return ((year % 4U) == 0U) ? 1U : 0U;
}

static uint8_t PCA2131_DaysInMonth(uint8_t month, uint8_t year)
{
    static const uint8_t days_in_month[12] =
    {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };

    if ((month < 1U) || (month > 12U))
    {
        return 0U;
    }

    if ((month == 2U) && (PCA2131_IsLeapYear(year) != 0U))
    {
        return 29U;
    }

    return days_in_month[month - 1U];
}

static PCA2131_OperationStatus_t PCA2131_CreateStatus(
    PCA2131_Result_t result,
    HAL_StatusTypeDef hal_status,
    uint32_t hal_error)
{
    PCA2131_OperationStatus_t status;

    status.result = result;
    status.hal_status = hal_status;
    status.hal_error = hal_error;
    status.recovery_attempted = 0U;
    status.recovery_status = HAL_OK;
    status.recovery_error = 0U;

    return status;
}

static void PCA2131_TryReleaseStop(
    const PCA2131_Device_t *device,
    uint8_t control1,
    PCA2131_OperationStatus_t *operation_status)
{
    control1 &= (uint8_t)(~PCA2131_CONTROL1_STOP_BIT);

    operation_status->recovery_attempted = 1U;
    operation_status->recovery_status = HAL_I2C_Mem_Write(
        device->i2c,
        PCA2131_I2C_ADDR,
        PCA2131_REG_CONTROL_1,
        I2C_MEMADD_SIZE_8BIT,
        &control1,
        1U,
        PCA2131_I2C_TIMEOUT_MS
    );

    operation_status->recovery_error = HAL_I2C_GetError(device->i2c);
}

void PCA2131_Driver_Init(PCA2131_Device_t *device,
                         I2C_HandleTypeDef *i2c)
{
    if (device != NULL)
    {
        device->i2c = i2c;
    }
}

uint8_t PCA2131_Driver_IsValidDateTime(
    const PCA2131_DateTime_t *date_time)
{
    uint8_t maximum_day;

    if (date_time == NULL)
    {
        return 0U;
    }

    if ((date_time->hundredth > 99U) ||
        (date_time->second > 59U) ||
        (date_time->minute > 59U) ||
        (date_time->hour > 23U) ||
        (date_time->weekday > 6U) ||
        (date_time->year > 99U))
    {
        return 0U;
    }

    maximum_day = PCA2131_DaysInMonth(date_time->month,
                                      date_time->year);

    if ((date_time->day < 1U) || (date_time->day > maximum_day))
    {
        return 0U;
    }

    return 1U;
}

PCA2131_OperationStatus_t PCA2131_Driver_CheckReady(
    const PCA2131_Device_t *device)
{
    HAL_StatusTypeDef hal_status;
    uint32_t hal_error;

    if ((device == NULL) || (device->i2c == NULL))
    {
        return PCA2131_CreateStatus(PCA2131_RESULT_INVALID_ARGUMENT,
                                    HAL_ERROR,
                                    0U);
    }

    hal_status = HAL_I2C_IsDeviceReady(device->i2c,
                                       PCA2131_I2C_ADDR,
                                       5U,
                                       PCA2131_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(device->i2c);

    if (hal_status != HAL_OK)
    {
        return PCA2131_CreateStatus(PCA2131_RESULT_DEVICE_NOT_READY,
                                    hal_status,
                                    hal_error);
    }

    return PCA2131_CreateStatus(PCA2131_RESULT_OK,
                                hal_status,
                                hal_error);
}

PCA2131_OperationStatus_t PCA2131_Driver_ReadDateTime(
    const PCA2131_Device_t *device,
    PCA2131_DateTime_t *date_time)
{
    HAL_StatusTypeDef hal_status;
    uint32_t hal_error;
    uint8_t raw_data[8] = {0U};

    if ((device == NULL) || (device->i2c == NULL) ||
        (date_time == NULL))
    {
        return PCA2131_CreateStatus(PCA2131_RESULT_INVALID_ARGUMENT,
                                    HAL_ERROR,
                                    0U);
    }

    hal_status = HAL_I2C_Mem_Read(device->i2c,
                                  PCA2131_I2C_ADDR,
                                  PCA2131_REG_100TH_SECONDS,
                                  I2C_MEMADD_SIZE_8BIT,
                                  raw_data,
                                  8U,
                                  PCA2131_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(device->i2c);

    if (hal_status != HAL_OK)
    {
        return PCA2131_CreateStatus(PCA2131_RESULT_READ_FAILED,
                                    hal_status,
                                    hal_error);
    }

    date_time->hundredth = PCA2131_BcdToDec(raw_data[0]);
    date_time->osf = (raw_data[1] >> 7) & 0x01U;
    date_time->second = PCA2131_BcdToDec(raw_data[1] & 0x7FU);
    date_time->minute = PCA2131_BcdToDec(raw_data[2] & 0x7FU);
    date_time->hour = PCA2131_BcdToDec(raw_data[3] & 0x3FU);
    date_time->day = PCA2131_BcdToDec(raw_data[4] & 0x3FU);
    date_time->weekday = raw_data[5] & 0x07U;
    date_time->month = PCA2131_BcdToDec(raw_data[6] & 0x1FU);
    date_time->year = PCA2131_BcdToDec(raw_data[7]);

    if ((date_time->osf == 0U) &&
        (PCA2131_IsValidBcd(raw_data[0]) != 0U) &&
        (PCA2131_IsValidBcd(raw_data[1] & 0x7FU) != 0U) &&
        (PCA2131_IsValidBcd(raw_data[2] & 0x7FU) != 0U) &&
        (PCA2131_IsValidBcd(raw_data[3] & 0x3FU) != 0U) &&
        (PCA2131_IsValidBcd(raw_data[4] & 0x3FU) != 0U) &&
        (PCA2131_IsValidBcd(raw_data[6] & 0x1FU) != 0U) &&
        (PCA2131_IsValidBcd(raw_data[7]) != 0U) &&
        (PCA2131_Driver_IsValidDateTime(date_time) != 0U))
    {
        date_time->calendar_valid = 1U;
    }
    else
    {
        date_time->calendar_valid = 0U;
    }

    return PCA2131_CreateStatus(PCA2131_RESULT_OK,
                                hal_status,
                                hal_error);
}

PCA2131_OperationStatus_t PCA2131_Driver_ReadAlarm(
    const PCA2131_Device_t *device,
    PCA2131_Alarm_t *alarm)
{
    HAL_StatusTypeDef hal_status;
    uint32_t hal_error;
    uint8_t control_data[2] = {0U};
    uint8_t alarm_data[5] = {0U};
    uint8_t configuration_valid = 1U;
    uint8_t hour_mask;
    uint8_t hour_minimum;
    uint8_t hour_maximum;

    if ((device == NULL) || (device->i2c == NULL) || (alarm == NULL))
    {
        return PCA2131_CreateStatus(PCA2131_RESULT_INVALID_ARGUMENT,
                                    HAL_ERROR,
                                    0U);
    }

    *alarm = (PCA2131_Alarm_t){0};

    /* Control_1 and Control_2 are adjacent and read in one transaction. */
    hal_status = HAL_I2C_Mem_Read(device->i2c,
                                  PCA2131_I2C_ADDR,
                                  PCA2131_REG_CONTROL_1,
                                  I2C_MEMADD_SIZE_8BIT,
                                  control_data,
                                  2U,
                                  PCA2131_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(device->i2c);

    if (hal_status != HAL_OK)
    {
        return PCA2131_CreateStatus(PCA2131_RESULT_ALARM_READ_FAILED,
                                    hal_status,
                                    hal_error);
    }

    hal_status = HAL_I2C_Mem_Read(device->i2c,
                                  PCA2131_I2C_ADDR,
                                  PCA2131_REG_SECOND_ALARM,
                                  I2C_MEMADD_SIZE_8BIT,
                                  alarm_data,
                                  5U,
                                  PCA2131_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(device->i2c);

    if (hal_status != HAL_OK)
    {
        return PCA2131_CreateStatus(PCA2131_RESULT_ALARM_READ_FAILED,
                                    hal_status,
                                    hal_error);
    }

    alarm->hour_mode_12 =
        ((control_data[0] & PCA2131_CONTROL1_12_24_BIT) != 0U) ? 1U : 0U;
    alarm->alarm_flag =
        ((control_data[1] & PCA2131_CONTROL2_AF_BIT) != 0U) ? 1U : 0U;
    alarm->interrupt_enabled =
        ((control_data[1] & PCA2131_CONTROL2_AIE_BIT) != 0U) ? 1U : 0U;

    configuration_valid &= PCA2131_DecodeAlarmBcdField(
        alarm_data[0], 0x7FU, 0U, 59U, &alarm->second);
    configuration_valid &= PCA2131_DecodeAlarmBcdField(
        alarm_data[1], 0x7FU, 0U, 59U, &alarm->minute);

    if (alarm->hour_mode_12 != 0U)
    {
        hour_mask = 0x1FU;
        hour_minimum = 1U;
        hour_maximum = 12U;
        alarm->hour_pm =
            ((alarm_data[2] & PCA2131_ALARM_PM_BIT) != 0U) ? 1U : 0U;
    }
    else
    {
        hour_mask = 0x3FU;
        hour_minimum = 0U;
        hour_maximum = 23U;
        alarm->hour_pm = 0U;
    }

    configuration_valid &= PCA2131_DecodeAlarmBcdField(
        alarm_data[2],
        hour_mask,
        hour_minimum,
        hour_maximum,
        &alarm->hour);
    configuration_valid &= PCA2131_DecodeAlarmBcdField(
        alarm_data[3], 0x3FU, 1U, 31U, &alarm->day);
    configuration_valid &= PCA2131_DecodeAlarmWeekday(
        alarm_data[4], &alarm->weekday);

    alarm->configuration_valid = configuration_valid;

    return PCA2131_CreateStatus(PCA2131_RESULT_OK,
                                hal_status,
                                hal_error);
}

uint8_t PCA2131_Driver_IsValidAlarmConfig(
    const PCA2131_AlarmConfig_t *config)
{
    if (config == NULL)
    {
        return 0U;
    }

    if ((PCA2131_IsValidAlarmField(&config->second, 0U, 59U) == 0U) ||
        (PCA2131_IsValidAlarmField(&config->minute, 0U, 59U) == 0U) ||
        (PCA2131_IsValidAlarmField(&config->hour, 0U, 23U) == 0U) ||
        (PCA2131_IsValidAlarmField(&config->day, 1U, 31U) == 0U) ||
        (PCA2131_IsValidAlarmField(&config->weekday, 0U, 6U) == 0U))
    {
        return 0U;
    }

    return 1U;
}

PCA2131_OperationStatus_t PCA2131_Driver_WriteAlarm(
    const PCA2131_Device_t *device,
    const PCA2131_AlarmConfig_t *config)
{
    HAL_StatusTypeDef hal_status;
    uint32_t hal_error;
    uint8_t control1;
    uint8_t alarm_data[5];
    uint8_t hour_mode_12;

    if ((device == NULL) || (device->i2c == NULL) || (config == NULL))
    {
        return PCA2131_CreateStatus(PCA2131_RESULT_INVALID_ARGUMENT,
                                    HAL_ERROR,
                                    0U);
    }

    if (PCA2131_Driver_IsValidAlarmConfig(config) == 0U)
    {
        return PCA2131_CreateStatus(PCA2131_RESULT_INVALID_ALARM_CONFIG,
                                    HAL_ERROR,
                                    0U);
    }

    hal_status = HAL_I2C_Mem_Read(device->i2c,
                                  PCA2131_I2C_ADDR,
                                  PCA2131_REG_CONTROL_1,
                                  I2C_MEMADD_SIZE_8BIT,
                                  &control1,
                                  1U,
                                  PCA2131_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(device->i2c);

    if (hal_status != HAL_OK)
    {
        return PCA2131_CreateStatus(
            PCA2131_RESULT_ALARM_CONTROL_READ_FAILED,
            hal_status,
            hal_error);
    }

    hour_mode_12 = ((control1 & PCA2131_CONTROL1_12_24_BIT) != 0U)
                 ? 1U
                 : 0U;

    alarm_data[0] = PCA2131_EncodeAlarmField(&config->second, 0x7FU);
    alarm_data[1] = PCA2131_EncodeAlarmField(&config->minute, 0x7FU);
    alarm_data[2] = PCA2131_EncodeAlarmHour(&config->hour, hour_mode_12);
    alarm_data[3] = PCA2131_EncodeAlarmField(&config->day, 0x3FU);

    if (config->weekday.enabled == 0U)
    {
        alarm_data[4] = PCA2131_ALARM_DISABLE_BIT;
    }
    else
    {
        alarm_data[4] = config->weekday.value & 0x07U;
    }

    hal_status = HAL_I2C_Mem_Write(device->i2c,
                                   PCA2131_I2C_ADDR,
                                   PCA2131_REG_SECOND_ALARM,
                                   I2C_MEMADD_SIZE_8BIT,
                                   alarm_data,
                                   5U,
                                   PCA2131_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(device->i2c);

    if (hal_status != HAL_OK)
    {
        return PCA2131_CreateStatus(PCA2131_RESULT_ALARM_WRITE_FAILED,
                                    hal_status,
                                    hal_error);
    }

    return PCA2131_CreateStatus(PCA2131_RESULT_OK,
                                hal_status,
                                hal_error);
}

PCA2131_OperationStatus_t PCA2131_Driver_ReadAlarmStatus(
    const PCA2131_Device_t *device,
    PCA2131_AlarmStatus_t *status)
{
    HAL_StatusTypeDef hal_status;
    uint32_t hal_error;
    uint8_t control2;

    if ((device == NULL) || (device->i2c == NULL) || (status == NULL))
    {
        return PCA2131_CreateStatus(PCA2131_RESULT_INVALID_ARGUMENT,
                                    HAL_ERROR,
                                    0U);
    }

    *status = (PCA2131_AlarmStatus_t){0};

    hal_status = HAL_I2C_Mem_Read(device->i2c,
                                  PCA2131_I2C_ADDR,
                                  PCA2131_REG_CONTROL_2,
                                  I2C_MEMADD_SIZE_8BIT,
                                  &control2,
                                  1U,
                                  PCA2131_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(device->i2c);

    if (hal_status != HAL_OK)
    {
        return PCA2131_CreateStatus(
            PCA2131_RESULT_ALARM_STATUS_READ_FAILED,
            hal_status,
            hal_error);
    }

    status->alarm_flag =
        ((control2 & PCA2131_CONTROL2_AF_BIT) != 0U) ? 1U : 0U;
    status->interrupt_enabled =
        ((control2 & PCA2131_CONTROL2_AIE_BIT) != 0U) ? 1U : 0U;

    return PCA2131_CreateStatus(PCA2131_RESULT_OK,
                                hal_status,
                                hal_error);
}

PCA2131_OperationStatus_t PCA2131_Driver_SetAlarmInterruptEnabled(
    const PCA2131_Device_t *device,
    uint8_t enabled)
{
    HAL_StatusTypeDef hal_status;
    uint32_t hal_error;
    uint8_t control2_write;

    if ((device == NULL) || (device->i2c == NULL) || (enabled > 1U))
    {
        return PCA2131_CreateStatus(PCA2131_RESULT_INVALID_ARGUMENT,
                                    HAL_ERROR,
                                    0U);
    }

    /*
     * AF, MSF and WDTF use write-AND clearing semantics. Writing 1 preserves
     * each flag even if it changes immediately before this transaction.
     */
    control2_write = PCA2131_CONTROL2_MSF_BIT |
                     PCA2131_CONTROL2_WDTF_BIT |
                     PCA2131_CONTROL2_AF_BIT;

    if (enabled != 0U)
    {
        control2_write |= PCA2131_CONTROL2_AIE_BIT;
    }

    hal_status = HAL_I2C_Mem_Write(device->i2c,
                                   PCA2131_I2C_ADDR,
                                   PCA2131_REG_CONTROL_2,
                                   I2C_MEMADD_SIZE_8BIT,
                                   &control2_write,
                                   1U,
                                   PCA2131_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(device->i2c);

    if (hal_status != HAL_OK)
    {
        return PCA2131_CreateStatus(
            PCA2131_RESULT_ALARM_INTERRUPT_WRITE_FAILED,
            hal_status,
            hal_error);
    }

    return PCA2131_CreateStatus(PCA2131_RESULT_OK,
                                hal_status,
                                hal_error);
}

PCA2131_OperationStatus_t PCA2131_Driver_ClearAlarmFlag(
    const PCA2131_Device_t *device)
{
    HAL_StatusTypeDef hal_status;
    uint32_t hal_error;
    uint8_t control2;
    uint8_t control2_write;

    if ((device == NULL) || (device->i2c == NULL))
    {
        return PCA2131_CreateStatus(PCA2131_RESULT_INVALID_ARGUMENT,
                                    HAL_ERROR,
                                    0U);
    }

    hal_status = HAL_I2C_Mem_Read(device->i2c,
                                  PCA2131_I2C_ADDR,
                                  PCA2131_REG_CONTROL_2,
                                  I2C_MEMADD_SIZE_8BIT,
                                  &control2,
                                  1U,
                                  PCA2131_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(device->i2c);

    if (hal_status != HAL_OK)
    {
        return PCA2131_CreateStatus(
            PCA2131_RESULT_ALARM_STATUS_READ_FAILED,
            hal_status,
            hal_error);
    }

    /*
     * Preserve MSF, WDTF and the current AIE setting; write AF=0 to clear
     * only the alarm flag. WDTF uses write-zero-to-clear semantics.
     */
    control2_write = PCA2131_CONTROL2_MSF_BIT |
                     PCA2131_CONTROL2_WDTF_BIT |
                     (control2 & PCA2131_CONTROL2_AIE_BIT);

    hal_status = HAL_I2C_Mem_Write(device->i2c,
                                   PCA2131_I2C_ADDR,
                                   PCA2131_REG_CONTROL_2,
                                   I2C_MEMADD_SIZE_8BIT,
                                   &control2_write,
                                   1U,
                                   PCA2131_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(device->i2c);

    if (hal_status != HAL_OK)
    {
        return PCA2131_CreateStatus(
            PCA2131_RESULT_ALARM_FLAG_CLEAR_FAILED,
            hal_status,
            hal_error);
    }

    return PCA2131_CreateStatus(PCA2131_RESULT_OK,
                                hal_status,
                                hal_error);
}

PCA2131_OperationStatus_t PCA2131_Driver_ReadWatchdogStatus(
    const PCA2131_Device_t *device,
    PCA2131_WatchdogStatus_t *status)
{
    HAL_StatusTypeDef hal_status;
    uint32_t hal_error;
    uint8_t control2;
    uint8_t interrupt_and_watchdog[5] = {0U};

    if ((device == NULL) || (device->i2c == NULL) || (status == NULL))
    {
        return PCA2131_CreateStatus(PCA2131_RESULT_INVALID_ARGUMENT,
                                    HAL_ERROR,
                                    0U);
    }

    *status = (PCA2131_WatchdogStatus_t){0};

    hal_status = HAL_I2C_Mem_Read(device->i2c,
                                  PCA2131_I2C_ADDR,
                                  PCA2131_REG_CONTROL_2,
                                  I2C_MEMADD_SIZE_8BIT,
                                  &control2,
                                  1U,
                                  PCA2131_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(device->i2c);

    if (hal_status != HAL_OK)
    {
        return PCA2131_CreateStatus(
            PCA2131_RESULT_WATCHDOG_STATUS_READ_FAILED,
            hal_status,
            hal_error);
    }

    /*
     * Read 31h..35h in one transaction:
     * INT_A_MASK1, INT_A_MASK2, INT_B_MASK1, INT_B_MASK2,
     * Watchdg_tim_ctl. Register 36h is write-only and is not read.
     */
    hal_status = HAL_I2C_Mem_Read(device->i2c,
                                  PCA2131_I2C_ADDR,
                                  PCA2131_REG_INT_A_MASK1,
                                  I2C_MEMADD_SIZE_8BIT,
                                  interrupt_and_watchdog,
                                  5U,
                                  PCA2131_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(device->i2c);

    if (hal_status != HAL_OK)
    {
        return PCA2131_CreateStatus(
            PCA2131_RESULT_WATCHDOG_STATUS_READ_FAILED,
            hal_status,
            hal_error);
    }

    status->raw_control_2 = control2;
    status->raw_int_a_mask1 = interrupt_and_watchdog[0];
    status->raw_int_b_mask1 = interrupt_and_watchdog[2];
    status->raw_watchdog_control = interrupt_and_watchdog[4];
    status->timeout_flag =
        ((control2 & PCA2131_CONTROL2_WDTF_BIT) != 0U) ? 1U : 0U;
    status->enabled =
        ((status->raw_watchdog_control &
          PCA2131_WATCHDOG_ENABLE_BIT) != 0U) ? 1U : 0U;
    status->clock_source = (PCA2131_WatchdogClock_t)
        (status->raw_watchdog_control & PCA2131_WATCHDOG_CLOCK_MASK);
    status->interrupt_a_masked =
        ((status->raw_int_a_mask1 & PCA2131_WATCHDOG_MASK_BIT) != 0U)
        ? 1U
        : 0U;
    status->interrupt_b_masked =
        ((status->raw_int_b_mask1 & PCA2131_WATCHDOG_MASK_BIT) != 0U)
        ? 1U
        : 0U;

    return PCA2131_CreateStatus(PCA2131_RESULT_OK,
                                hal_status,
                                hal_error);
}

PCA2131_OperationStatus_t PCA2131_Driver_WriteDateTime(
    const PCA2131_Device_t *device,
    const PCA2131_DateTime_t *date_time)
{
    PCA2131_OperationStatus_t status;
    HAL_StatusTypeDef hal_status;
    uint32_t hal_error;
    uint8_t control1;
    uint8_t cpr = PCA2131_SR_RESET_CPR;
    uint8_t calendar_data[8];

    if ((device == NULL) || (device->i2c == NULL) ||
        (date_time == NULL))
    {
        return PCA2131_CreateStatus(PCA2131_RESULT_INVALID_ARGUMENT,
                                    HAL_ERROR,
                                    0U);
    }

    if (PCA2131_Driver_IsValidDateTime(date_time) == 0U)
    {
        return PCA2131_CreateStatus(PCA2131_RESULT_INVALID_DATETIME,
                                    HAL_ERROR,
                                    0U);
    }

    hal_status = HAL_I2C_Mem_Read(device->i2c,
                                  PCA2131_I2C_ADDR,
                                  PCA2131_REG_CONTROL_1,
                                  I2C_MEMADD_SIZE_8BIT,
                                  &control1,
                                  1U,
                                  PCA2131_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(device->i2c);

    if (hal_status != HAL_OK)
    {
        return PCA2131_CreateStatus(PCA2131_RESULT_CONTROL_READ_FAILED,
                                    hal_status,
                                    hal_error);
    }

    control1 &= (uint8_t)(~(PCA2131_CONTROL1_100TH_S_DIS |
                            PCA2131_CONTROL1_12_24_BIT));
    control1 |= PCA2131_CONTROL1_STOP_BIT;

    hal_status = HAL_I2C_Mem_Write(device->i2c,
                                   PCA2131_I2C_ADDR,
                                   PCA2131_REG_CONTROL_1,
                                   I2C_MEMADD_SIZE_8BIT,
                                   &control1,
                                   1U,
                                   PCA2131_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(device->i2c);

    if (hal_status != HAL_OK)
    {
        status = PCA2131_CreateStatus(PCA2131_RESULT_STOP_WRITE_FAILED,
                                      hal_status,
                                      hal_error);
        PCA2131_TryReleaseStop(device, control1, &status);
        return status;
    }

    hal_status = HAL_I2C_Mem_Write(device->i2c,
                                   PCA2131_I2C_ADDR,
                                   PCA2131_REG_SR_RESET,
                                   I2C_MEMADD_SIZE_8BIT,
                                   &cpr,
                                   1U,
                                   PCA2131_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(device->i2c);

    if (hal_status != HAL_OK)
    {
        status = PCA2131_CreateStatus(PCA2131_RESULT_CPR_WRITE_FAILED,
                                      hal_status,
                                      hal_error);
        PCA2131_TryReleaseStop(device, control1, &status);
        return status;
    }

    calendar_data[0] = PCA2131_DecToBcd(date_time->hundredth);
    calendar_data[1] = PCA2131_DecToBcd(date_time->second) & 0x7FU;
    calendar_data[2] = PCA2131_DecToBcd(date_time->minute) & 0x7FU;
    calendar_data[3] = PCA2131_DecToBcd(date_time->hour) & 0x3FU;
    calendar_data[4] = PCA2131_DecToBcd(date_time->day) & 0x3FU;
    calendar_data[5] = date_time->weekday & 0x07U;
    calendar_data[6] = PCA2131_DecToBcd(date_time->month) & 0x1FU;
    calendar_data[7] = PCA2131_DecToBcd(date_time->year);

    hal_status = HAL_I2C_Mem_Write(device->i2c,
                                   PCA2131_I2C_ADDR,
                                   PCA2131_REG_100TH_SECONDS,
                                   I2C_MEMADD_SIZE_8BIT,
                                   calendar_data,
                                   8U,
                                   PCA2131_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(device->i2c);

    if (hal_status != HAL_OK)
    {
        status = PCA2131_CreateStatus(PCA2131_RESULT_CALENDAR_WRITE_FAILED,
                                      hal_status,
                                      hal_error);
        PCA2131_TryReleaseStop(device, control1, &status);
        return status;
    }

    control1 &= (uint8_t)(~PCA2131_CONTROL1_STOP_BIT);

    hal_status = HAL_I2C_Mem_Write(device->i2c,
                                   PCA2131_I2C_ADDR,
                                   PCA2131_REG_CONTROL_1,
                                   I2C_MEMADD_SIZE_8BIT,
                                   &control1,
                                   1U,
                                   PCA2131_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(device->i2c);

    if (hal_status != HAL_OK)
    {
        status = PCA2131_CreateStatus(PCA2131_RESULT_START_WRITE_FAILED,
                                      hal_status,
                                      hal_error);
        PCA2131_TryReleaseStop(device, control1, &status);
        return status;
    }

    return PCA2131_CreateStatus(PCA2131_RESULT_OK,
                                hal_status,
                                hal_error);
}
