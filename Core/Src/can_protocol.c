#include "can_protocol.h"

#include <stddef.h>

uint16_t CAN_Protocol_ReadU16LE(const uint8_t *data)
{
    return ((uint16_t)data[0])
         | ((uint16_t)data[1] << 8);
}

uint32_t CAN_Protocol_ReadU32LE(const uint8_t *data)
{
    return ((uint32_t)data[0])
         | ((uint32_t)data[1] << 8)
         | ((uint32_t)data[2] << 16)
         | ((uint32_t)data[3] << 24);
}

void CAN_Protocol_WriteU32LE(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8) & 0xFFU);
    data[2] = (uint8_t)((value >> 16) & 0xFFU);
    data[3] = (uint8_t)((value >> 24) & 0xFFU);
}

void CAN_Protocol_WriteU16LE(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

uint8_t CAN_Protocol_IsValidId(uint8_t is_extended,
                               uint32_t identifier)
{
    if (is_extended == 0U)
    {
        return (identifier <= 0x7FFU) ? 1U : 0U;
    }

    return (identifier <= 0x1FFFFFFFUL) ? 1U : 0U;
}

uint8_t CAN_Protocol_DecodeRtcAlarmCommand(
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE],
    CAN_Protocol_RtcAlarmCommand_t *command)
{
    uint8_t enable_mask;

    if ((payload == NULL) || (command == NULL))
    {
        return 0U;
    }

    enable_mask = payload[1];

    if (((enable_mask & (uint8_t)(~CAN_PROTOCOL_RTC_ALARM_ENABLE_MASK)) !=
         0U) ||
        (payload[7] != 0U))
    {
        return 0U;
    }

    if (((enable_mask & CAN_PROTOCOL_RTC_ALARM_ENABLE_SECOND) != 0U) ?
            (payload[2] > 59U) : (payload[2] != 0U))
    {
        return 0U;
    }

    if (((enable_mask & CAN_PROTOCOL_RTC_ALARM_ENABLE_MINUTE) != 0U) ?
            (payload[3] > 59U) : (payload[3] != 0U))
    {
        return 0U;
    }

    if (((enable_mask & CAN_PROTOCOL_RTC_ALARM_ENABLE_HOUR) != 0U) ?
            (payload[4] > 23U) : (payload[4] != 0U))
    {
        return 0U;
    }

    if (((enable_mask & CAN_PROTOCOL_RTC_ALARM_ENABLE_DAY) != 0U) ?
            ((payload[5] < 1U) || (payload[5] > 31U)) :
            (payload[5] != 0U))
    {
        return 0U;
    }

    if (((enable_mask & CAN_PROTOCOL_RTC_ALARM_ENABLE_WEEKDAY) != 0U) ?
            (payload[6] > 6U) : (payload[6] != 0U))
    {
        return 0U;
    }

    command->enable_mask = enable_mask;
    command->second = payload[2];
    command->minute = payload[3];
    command->hour = payload[4];
    command->day = payload[5];
    command->weekday = payload[6];

    return 1U;
}

void CAN_Protocol_EncodeRtcStatus(CAN_Protocol_RtcStatusCode_t status_code,
                                  uint8_t hal_status,
                                  uint32_t hal_error,
                                  uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    payload[0] = (uint8_t)status_code;
    payload[1] = hal_status;
    payload[2] = (uint8_t)(hal_error & 0xFFU);
    payload[3] = (uint8_t)((hal_error >> 8) & 0xFFU);
    payload[4] = (uint8_t)((hal_error >> 16) & 0xFFU);
    payload[5] = (uint8_t)((hal_error >> 24) & 0xFFU);
    payload[6] = 0U;
    payload[7] = 0U;
}

void CAN_Protocol_EncodeRtcTime(
    const CAN_Protocol_RtcTime_t *rtc_time,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    payload[0] = rtc_time->hour;
    payload[1] = rtc_time->minute;
    payload[2] = rtc_time->second;
    payload[3] = rtc_time->hundredth;
    payload[4] = rtc_time->day;
    payload[5] = rtc_time->month;
    payload[6] = rtc_time->year;
    payload[7] = rtc_time->weekday & 0x07U;

    if (rtc_time->calendar_valid != 0U)
    {
        payload[7] |= 0x20U;
    }

    if (rtc_time->ready != 0U)
    {
        payload[7] |= 0x40U;
    }

    if (rtc_time->osf != 0U)
    {
        payload[7] |= 0x80U;
    }
}

void CAN_Protocol_EncodeRtcAlarmEvent(
    const CAN_Protocol_RtcAlarmEvent_t *event,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    payload[0] = (uint8_t)event->event_code;
    payload[1] = 0U;

    if (event->alarm_flag != 0U)
    {
        payload[1] |= CAN_PROTOCOL_RTC_ALARM_EVENT_AF;
    }

    if (event->interrupt_enabled != 0U)
    {
        payload[1] |= CAN_PROTOCOL_RTC_ALARM_EVENT_AIE;
    }

    if (event->configuration_valid != 0U)
    {
        payload[1] |= CAN_PROTOCOL_RTC_ALARM_EVENT_CONFIG_OK;
    }

    payload[2] = event->hour;
    payload[3] = event->minute;
    payload[4] = event->second;
    payload[5] = event->day;
    payload[6] = event->month;
    payload[7] = event->year;
}

void CAN_Protocol_EncodeSystemStatus(
    const CAN_Protocol_SystemStatus_t *system_status,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    payload[0] = 0U;

    if (system_status->slot_1_running != 0U)
    {
        payload[0] |= 0x01U;
    }

    if (system_status->slot_2_running != 0U)
    {
        payload[0] |= 0x02U;
    }

    if (system_status->led_1_on != 0U)
    {
        payload[0] |= 0x04U;
    }

    if (system_status->led_2_on != 0U)
    {
        payload[0] |= 0x08U;
    }

    payload[1] = system_status->slot_1_running;
    payload[2] = system_status->slot_2_running;
    payload[3] = system_status->led_1_on;
    payload[4] = system_status->led_2_on;
    payload[5] = 0U;
    payload[6] = 0U;
    payload[7] = 0U;
}

void CAN_Protocol_EncodeLogHeartbeat(
    const CAN_Protocol_LogHeartbeat_t *heartbeat,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    payload[0] = CAN_PROTOCOL_LOG_VERSION;
    payload[1] = 0U;

    if (heartbeat->ready != 0U)
    {
        payload[1] |= CAN_PROTOCOL_LOG_HEARTBEAT_FLAG_READY;
    }

    if (heartbeat->overwrite_detected != 0U)
    {
        payload[1] |= CAN_PROTOCOL_LOG_HEARTBEAT_FLAG_OVERWRITE;
    }

    CAN_Protocol_WriteU32LE(&payload[2], heartbeat->latest_sequence);
    payload[6] = heartbeat->record_count;
    payload[7] = heartbeat->alive_counter;
}

void CAN_Protocol_EncodePwmStatus(
    const CAN_Protocol_PwmStatus_t *pwm_status,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    payload[0] = pwm_status->running;
    payload[1] = pwm_status->duty_percent;
    CAN_Protocol_WriteU32LE(&payload[2], pwm_status->actual_frequency_hz);
    payload[6] = 0U;
    payload[7] = 0U;
}

void CAN_Protocol_EncodeInputCaptureStatus(
    const CAN_Protocol_InputCaptureStatus_t *status,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    payload[0] = status->signal_detected;
    payload[1] = status->duty_percent;
    CAN_Protocol_WriteU32LE(&payload[2], status->frequency_hz);
    CAN_Protocol_WriteU16LE(&payload[6], status->edge_count);
}
