#ifndef CAN_PROTOCOL_H
#define CAN_PROTOCOL_H

#include <stdint.h>

#include "can_protocol_generated.h"

#define CAN_PROTOCOL_LOG_VERSION               1U
#define CAN_PROTOCOL_LOG_FRAGMENT_DATA_SIZE    7U
#define CAN_PROTOCOL_LOG_INFO_WIRE_SIZE        18U
#define CAN_PROTOCOL_LOG_INFO_FRAGMENT_BASE    0x70U
#define CAN_PROTOCOL_LOG_RECORD_FRAGMENT_BASE  0x80U
#define CAN_PROTOCOL_LOG_ERROR_FRAME           0xF0U

#define CAN_PROTOCOL_LOG_HEARTBEAT_FLAG_READY      0x01U
#define CAN_PROTOCOL_LOG_HEARTBEAT_FLAG_OVERWRITE  0x02U

typedef enum
{
    CAN_PROTOCOL_LOG_ERROR_SEQUENCE_NOT_FOUND = 0x01U
} CAN_Protocol_LogError_t;

typedef struct
{
    uint8_t enable_mask;
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t weekday;
} CAN_Protocol_RtcAlarmCommand_t;

typedef struct
{
    CAN_Protocol_RtcAlarmEventCode_t event_code;
    uint8_t alarm_flag;
    uint8_t interrupt_enabled;
    uint8_t configuration_valid;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t day;
    uint8_t month;
    uint8_t year;
} CAN_Protocol_RtcAlarmEvent_t;

typedef struct
{
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t hundredth;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t weekday;
    uint8_t calendar_valid;
    uint8_t ready;
    uint8_t osf;
} CAN_Protocol_RtcTime_t;

typedef struct
{
    uint8_t slot_1_running;
    uint8_t slot_2_running;
    uint8_t led_1_on;
    uint8_t led_2_on;
    uint8_t request_flags;
    uint8_t physical_flags;
    uint8_t override_flags;
} CAN_Protocol_SystemStatus_t;

typedef struct
{
    uint8_t flags;
    uint8_t max_fifo_fill;
    uint16_t message_lost_events;
    uint16_t fifo_full_events;
    uint16_t watermark_events;
} CAN_Protocol_CanRxHealth_t;

typedef struct
{
    uint32_t latest_sequence;
    uint8_t record_count;
    uint8_t ready;
    uint8_t overwrite_detected;
    uint8_t alive_counter;
} CAN_Protocol_LogHeartbeat_t;

typedef struct
{
    uint8_t running;
    uint8_t duty_percent;
    uint32_t actual_frequency_hz;
    uint8_t control_flags;
} CAN_Protocol_PwmStatus_t;

typedef struct
{
    uint8_t signal_detected;
    uint8_t duty_percent;
    uint32_t frequency_hz;
    uint16_t edge_count;
} CAN_Protocol_InputCaptureStatus_t;

typedef struct
{
    uint8_t state;
    uint8_t current_point;
    uint8_t total_points;
    uint8_t passed_points;
    uint32_t expected_frequency_hz;
} CAN_Protocol_PwmSelfTestStatus_t;

typedef struct
{
    uint8_t point;
    uint8_t passed;
    uint8_t expected_duty_percent;
    uint8_t measured_duty_percent;
    uint32_t measured_frequency_hz;
} CAN_Protocol_PwmSelfTestResult_t;

typedef struct
{
    uint8_t online;
    uint8_t configuration_valid;
    uint8_t crc_complete;
    uint8_t monitoring;
    uint8_t adc_characterization;
    uint8_t por_observed;
    uint8_t service_fault;
    uint8_t device_id;
    uint8_t service_result;
    uint8_t spi_fail;
    uint8_t parity_fail;
    uint8_t switch_state_change;
    uint8_t supply_threshold;
    uint8_t temperature;
    uint8_t other_interrupt;
    uint8_t power_on_reset;
    uint16_t service_failures;
    uint16_t last_nonzero_int_status;
} CAN_Protocol_Tic12400Status_t;

typedef struct
{
    uint8_t generation;
    uint8_t group_index;
    uint16_t adc_code[CAN_PROTOCOL_TIC12400_ADC_CODES_PER_FRAME];
} CAN_Protocol_Tic12400AdcGroup_t;

typedef struct
{
    uint32_t closed_bitmap;
    uint32_t valid_mask;
    uint8_t generation;
    uint8_t data_valid;
} CAN_Protocol_Tic12400SwitchState_t;

typedef struct
{
    uint32_t battery_switch_mask;
    uint32_t battery_capable_mask;
    uint8_t generation;
    uint8_t configuration_valid;
} CAN_Protocol_Tic12400Profile_t;

uint16_t CAN_Protocol_ReadU16LE(const uint8_t *data);
uint32_t CAN_Protocol_ReadU32LE(const uint8_t *data);
void CAN_Protocol_WriteU32LE(uint8_t *data, uint32_t value);
void CAN_Protocol_WriteU16LE(uint8_t *data, uint16_t value);
uint8_t CAN_Protocol_CalculateCommandToken(
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

uint8_t CAN_Protocol_IsValidId(uint8_t is_extended,
                               uint32_t identifier);

uint8_t CAN_Protocol_DecodeRtcAlarmCommand(
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE],
    CAN_Protocol_RtcAlarmCommand_t *command);

uint8_t CAN_Protocol_DecodeTic12400PolarityCommand(
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE],
    uint32_t *battery_switch_mask);

void CAN_Protocol_EncodeRtcStatus(CAN_Protocol_RtcStatusCode_t status_code,
                                  uint8_t hal_status,
                                  uint32_t hal_error,
                                  uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

void CAN_Protocol_EncodeRtcTime(
    const CAN_Protocol_RtcTime_t *rtc_time,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

void CAN_Protocol_EncodeRtcAlarmEvent(
    const CAN_Protocol_RtcAlarmEvent_t *event,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

void CAN_Protocol_EncodeSystemStatus(
    const CAN_Protocol_SystemStatus_t *system_status,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

void CAN_Protocol_EncodeCanRxHealth(
    const CAN_Protocol_CanRxHealth_t *health,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

void CAN_Protocol_EncodeLogHeartbeat(
    const CAN_Protocol_LogHeartbeat_t *heartbeat,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

void CAN_Protocol_EncodePwmStatus(
    const CAN_Protocol_PwmStatus_t *pwm_status,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

void CAN_Protocol_EncodeInputCaptureStatus(
    const CAN_Protocol_InputCaptureStatus_t *status,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

void CAN_Protocol_EncodePwmSelfTestStatus(
    const CAN_Protocol_PwmSelfTestStatus_t *status,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

void CAN_Protocol_EncodePwmSelfTestResult(
    const CAN_Protocol_PwmSelfTestResult_t *result,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

void CAN_Protocol_EncodeTic12400Status(
    const CAN_Protocol_Tic12400Status_t *status,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

void CAN_Protocol_EncodeTic12400AdcGroup(
    const CAN_Protocol_Tic12400AdcGroup_t *group,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

void CAN_Protocol_EncodeTic12400SwitchState(
    const CAN_Protocol_Tic12400SwitchState_t *state,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

void CAN_Protocol_EncodeTic12400Profile(
    const CAN_Protocol_Tic12400Profile_t *profile,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

#endif /* CAN_PROTOCOL_H */
