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

uint8_t CAN_Protocol_CalculateCommandToken(
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    uint8_t bit;
    uint8_t crc = 0xFFU;
    uint8_t index;

    if (payload == NULL)
    {
        return 0U;
    }

    for (index = 0U; index < CAN_PROTOCOL_PAYLOAD_SIZE; index++)
    {
        crc ^= payload[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 0x80U) != 0U)
                ? (uint8_t)((crc << 1U) ^ 0x1DU)
                : (uint8_t)(crc << 1U);
        }
    }

    return crc ^ 0xFFU;
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

uint8_t CAN_Protocol_DecodeTic12400PolarityCommand(
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE],
    uint32_t *battery_switch_mask)
{
    uint32_t decoded_mask;

    if ((payload == NULL) || (battery_switch_mask == NULL))
    {
        return 0U;
    }

    decoded_mask =
        ((uint32_t)payload[1]) |
        ((uint32_t)payload[2] << 8U) |
        ((uint32_t)payload[3] << 16U);
    if (((decoded_mask &
          ~CAN_PROTOCOL_TIC12400_BATTERY_CAPABLE_MASK) != 0U) ||
        (payload[4] != 0U) ||
        (payload[5] != 0U) ||
        (payload[6] != 0U) ||
        (payload[7] != 0U))
    {
        return 0U;
    }

    *battery_switch_mask = decoded_mask;
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
    payload[5] = system_status->request_flags;
    payload[6] = system_status->physical_flags;
    payload[7] = system_status->override_flags;
}

void CAN_Protocol_EncodeCanRxHealth(
    const CAN_Protocol_CanRxHealth_t *health,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    payload[0] = health->flags;
    payload[1] = health->max_fifo_fill;
    CAN_Protocol_WriteU16LE(&payload[2], health->message_lost_events);
    CAN_Protocol_WriteU16LE(&payload[4], health->fifo_full_events);
    CAN_Protocol_WriteU16LE(&payload[6], health->watermark_events);
}

void CAN_Protocol_EncodeTimingService(
    const CAN_Protocol_TimingService_t *timing,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    payload[0] = timing->service_id;
    payload[1] = timing->flags;
    CAN_Protocol_WriteU16LE(&payload[2], timing->current_us);
    CAN_Protocol_WriteU16LE(&payload[4], timing->minimum_us);
    CAN_Protocol_WriteU16LE(&payload[6], timing->maximum_us);
}

void CAN_Protocol_EncodeTimingAckLatency(
    const CAN_Protocol_TimingAckLatency_t *timing,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    CAN_Protocol_WriteU16LE(&payload[0], timing->p50_us);
    CAN_Protocol_WriteU16LE(&payload[2], timing->p95_us);
    CAN_Protocol_WriteU16LE(&payload[4], timing->p99_us);
    CAN_Protocol_WriteU16LE(&payload[6], timing->maximum_us);
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
    payload[6] = pwm_status->control_flags;
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

void CAN_Protocol_EncodePwmSelfTestStatus(
    const CAN_Protocol_PwmSelfTestStatus_t *status,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    payload[0] = status->state;
    payload[1] = status->current_point;
    payload[2] = status->total_points;
    payload[3] = status->passed_points;
    CAN_Protocol_WriteU32LE(&payload[4], status->expected_frequency_hz);
}

void CAN_Protocol_EncodePwmSelfTestResult(
    const CAN_Protocol_PwmSelfTestResult_t *result,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    payload[0] = result->point;
    payload[1] = result->passed;
    payload[2] = result->expected_duty_percent;
    payload[3] = result->measured_duty_percent;
    CAN_Protocol_WriteU32LE(&payload[4], result->measured_frequency_hz);
}

void CAN_Protocol_EncodeTic12400Status(
    const CAN_Protocol_Tic12400Status_t *status,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    payload[0] = 0U;
    payload[3] = 0U;

    if (status->online != 0U)
    {
        payload[0] |= CAN_PROTOCOL_TIC12400_STATUS_ONLINE;
    }
    if (status->configuration_valid != 0U)
    {
        payload[0] |=
            CAN_PROTOCOL_TIC12400_STATUS_CONFIGURATION_VALID;
    }
    if (status->crc_complete != 0U)
    {
        payload[0] |= CAN_PROTOCOL_TIC12400_STATUS_CRC_COMPLETE;
    }
    if (status->monitoring != 0U)
    {
        payload[0] |= CAN_PROTOCOL_TIC12400_STATUS_MONITORING;
    }
    if (status->adc_characterization != 0U)
    {
        payload[0] |=
            CAN_PROTOCOL_TIC12400_STATUS_ADC_CHARACTERIZATION;
    }
    if (status->por_observed != 0U)
    {
        payload[0] |= CAN_PROTOCOL_TIC12400_STATUS_POR_OBSERVED;
    }
    if (status->service_fault != 0U)
    {
        payload[0] |= CAN_PROTOCOL_TIC12400_STATUS_SERVICE_FAULT;
    }

    payload[1] = status->device_id;
    payload[2] = status->service_result;

    if (status->spi_fail != 0U)
    {
        payload[3] |= CAN_PROTOCOL_TIC12400_TRANSACTION_SPI_FAIL;
    }
    if (status->parity_fail != 0U)
    {
        payload[3] |=
            CAN_PROTOCOL_TIC12400_TRANSACTION_PARITY_FAIL;
    }
    if (status->switch_state_change != 0U)
    {
        payload[3] |=
            CAN_PROTOCOL_TIC12400_TRANSACTION_SWITCH_STATE_CHANGE;
    }
    if (status->supply_threshold != 0U)
    {
        payload[3] |=
            CAN_PROTOCOL_TIC12400_TRANSACTION_SUPPLY_THRESHOLD;
    }
    if (status->temperature != 0U)
    {
        payload[3] |=
            CAN_PROTOCOL_TIC12400_TRANSACTION_TEMPERATURE;
    }
    if (status->other_interrupt != 0U)
    {
        payload[3] |=
            CAN_PROTOCOL_TIC12400_TRANSACTION_OTHER_INTERRUPT;
    }
    if (status->power_on_reset != 0U)
    {
        payload[3] |=
            CAN_PROTOCOL_TIC12400_TRANSACTION_POWER_ON_RESET;
    }

    CAN_Protocol_WriteU16LE(&payload[4], status->service_failures);
    CAN_Protocol_WriteU16LE(
        &payload[6],
        status->last_nonzero_int_status);
}

void CAN_Protocol_EncodeTic12400AdcGroup(
    const CAN_Protocol_Tic12400AdcGroup_t *group,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    uint8_t index;

    payload[0] = group->generation;
    payload[1] = group->group_index;

    for (index = 0U;
         index < CAN_PROTOCOL_TIC12400_ADC_CODES_PER_FRAME;
         index++)
    {
        CAN_Protocol_WriteU16LE(
            &payload[2U + (index * 2U)],
            (uint16_t)(
                group->adc_code[index] &
                CAN_PROTOCOL_TIC12400_ADC_CODE_MAX));
    }
}

void CAN_Protocol_EncodeTic12400SwitchState(
    const CAN_Protocol_Tic12400SwitchState_t *state,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    uint32_t closed = state->closed_bitmap & 0x00FFFFFFUL;
    uint32_t valid = state->valid_mask & 0x00FFFFFFUL;

    payload[0] = (uint8_t)closed;
    payload[1] = (uint8_t)(closed >> 8);
    payload[2] = (uint8_t)(closed >> 16);
    payload[3] = (uint8_t)valid;
    payload[4] = (uint8_t)(valid >> 8);
    payload[5] = (uint8_t)(valid >> 16);
    payload[6] = state->generation;
    payload[7] = (state->data_valid != 0U) ?
        CAN_PROTOCOL_TIC12400_SWITCH_DATA_VALID : 0U;
}

void CAN_Protocol_EncodeTic12400Profile(
    const CAN_Protocol_Tic12400Profile_t *profile,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE])
{
    payload[0] = (uint8_t)(profile->battery_switch_mask & 0xFFU);
    payload[1] =
        (uint8_t)((profile->battery_switch_mask >> 8U) & 0xFFU);
    payload[2] =
        (uint8_t)((profile->battery_switch_mask >> 16U) & 0xFFU);
    payload[3] = (uint8_t)(profile->battery_capable_mask & 0xFFU);
    payload[4] =
        (uint8_t)((profile->battery_capable_mask >> 8U) & 0xFFU);
    payload[5] =
        (uint8_t)((profile->battery_capable_mask >> 16U) & 0xFFU);
    payload[6] = profile->generation;
    payload[7] = (profile->configuration_valid != 0U) ?
        CAN_PROTOCOL_TIC12400_PROFILE_CONFIGURATION_VALID : 0U;
}
