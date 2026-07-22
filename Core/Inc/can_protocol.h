#ifndef CAN_PROTOCOL_H
#define CAN_PROTOCOL_H

#include <stdint.h>

#define CAN_PROTOCOL_PAYLOAD_SIZE              8U

#define CAN_PROTOCOL_GUI_COMMAND_ID_EXT        0x1894AABBUL
#define CAN_PROTOCOL_RTC_STATUS_TX_ID          0x551U
#define CAN_PROTOCOL_RTC_TIME_TX_ID            0x556U
#define CAN_PROTOCOL_SYSTEM_STATUS_TX_ID       0x557U
#define CAN_PROTOCOL_RTC_ALARM_EVENT_TX_ID     0x558U
#define CAN_PROTOCOL_LOG_RESPONSE_TX_ID        0x55AU
#define CAN_PROTOCOL_LOG_HEARTBEAT_TX_ID       0x55BU

#define CAN_PROTOCOL_LOG_VERSION               1U
#define CAN_PROTOCOL_LOG_FRAGMENT_DATA_SIZE    7U
#define CAN_PROTOCOL_LOG_INFO_WIRE_SIZE        18U
#define CAN_PROTOCOL_LOG_INFO_FRAGMENT_BASE    0x70U
#define CAN_PROTOCOL_LOG_RECORD_FRAGMENT_BASE  0x80U
#define CAN_PROTOCOL_LOG_ERROR_FRAME           0xF0U

#define CAN_PROTOCOL_LOG_HEARTBEAT_FLAG_READY      0x01U
#define CAN_PROTOCOL_LOG_HEARTBEAT_FLAG_OVERWRITE  0x02U

#define CAN_PROTOCOL_SLOT_FLAG_ENABLE          0x01U
#define CAN_PROTOCOL_SLOT_FLAG_EXTENDED_ID     0x02U

#define CAN_PROTOCOL_RTC_ALARM_ENABLE_SECOND   0x01U
#define CAN_PROTOCOL_RTC_ALARM_ENABLE_MINUTE   0x02U
#define CAN_PROTOCOL_RTC_ALARM_ENABLE_HOUR     0x04U
#define CAN_PROTOCOL_RTC_ALARM_ENABLE_DAY      0x08U
#define CAN_PROTOCOL_RTC_ALARM_ENABLE_WEEKDAY  0x10U
#define CAN_PROTOCOL_RTC_ALARM_ENABLE_MASK     0x1FU

#define CAN_PROTOCOL_RTC_ALARM_EVENT_AF         0x01U
#define CAN_PROTOCOL_RTC_ALARM_EVENT_AIE        0x02U
#define CAN_PROTOCOL_RTC_ALARM_EVENT_CONFIG_OK  0x04U

typedef enum
{
    CAN_PROTOCOL_CMD_SET_SLOT_1 = 0x01U,
    CAN_PROTOCOL_CMD_SET_SLOT_2 = 0x02U,
    CAN_PROTOCOL_CMD_LED_CONTROL = 0x10U,
    CAN_PROTOCOL_CMD_START_SLOT_1_COUNTER = 0x11U,
    CAN_PROTOCOL_CMD_START_SLOT_2_COUNTER = 0x12U,
    CAN_PROTOCOL_CMD_RTC_SET_TIME = 0x20U,
    CAN_PROTOCOL_CMD_RTC_SET_DATETIME = 0x21U,
    CAN_PROTOCOL_CMD_RTC_SET_ALARM = 0x22U,
    CAN_PROTOCOL_CMD_LOG_GET_INFO = 0x30U,
    CAN_PROTOCOL_CMD_LOG_READ_SEQUENCE = 0x31U
} CAN_Protocol_Command_t;

typedef enum
{
    CAN_PROTOCOL_LOG_ERROR_SEQUENCE_NOT_FOUND = 0x01U
} CAN_Protocol_LogError_t;

/*
 * RTC status byte values are part of the CAN wire protocol.
 * Keep the explicit values unchanged to preserve GUI compatibility.
 */
typedef enum
{
    CAN_PROTOCOL_RTC_STATUS_INIT_OK = 0xA1U,
    CAN_PROTOCOL_RTC_STATUS_WRITE_VERIFY_OK = 0xA2U,
    CAN_PROTOCOL_RTC_STATUS_RECONNECTED = 0xA3U,
    CAN_PROTOCOL_RTC_STATUS_ALARM_WRITE_VERIFY_OK = 0xA4U,

    CAN_PROTOCOL_RTC_STATUS_INIT_FAILED = 0xE1U,
    CAN_PROTOCOL_RTC_STATUS_READ_FAILED = 0xE2U,
    CAN_PROTOCOL_RTC_STATUS_INVALID_DATETIME = 0xE4U,
    CAN_PROTOCOL_RTC_STATUS_CONTROL_READ_FAILED = 0xE5U,
    CAN_PROTOCOL_RTC_STATUS_STOP_WRITE_FAILED = 0xE6U,
    CAN_PROTOCOL_RTC_STATUS_CPR_WRITE_FAILED = 0xE7U,
    CAN_PROTOCOL_RTC_STATUS_CALENDAR_WRITE_FAILED = 0xE8U,
    CAN_PROTOCOL_RTC_STATUS_START_WRITE_FAILED = 0xE9U,
    CAN_PROTOCOL_RTC_STATUS_RECOVERY_FAILED = 0xEAU,
    CAN_PROTOCOL_RTC_STATUS_BUSY = 0xEBU,
    CAN_PROTOCOL_RTC_STATUS_ALARM_NOT_READY = 0xECU,
    CAN_PROTOCOL_RTC_STATUS_INVALID_ALARM_CONFIG = 0xEDU,
    CAN_PROTOCOL_RTC_STATUS_ALARM_WRITE_FAILED = 0xEEU,
    CAN_PROTOCOL_RTC_STATUS_ALARM_READBACK_FAILED = 0xEFU,
    CAN_PROTOCOL_RTC_STATUS_ALARM_VERIFY_MISMATCH = 0xF0U,
    CAN_PROTOCOL_RTC_STATUS_ALARM_FLAG_CLEAR_FAILED = 0xF1U
} CAN_Protocol_RtcStatusCode_t;

typedef enum
{
    CAN_PROTOCOL_RTC_ALARM_EVENT_TRIGGERED = 0x01U
} CAN_Protocol_RtcAlarmEventCode_t;

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
} CAN_Protocol_SystemStatus_t;

typedef struct
{
    uint32_t latest_sequence;
    uint8_t record_count;
    uint8_t ready;
    uint8_t overwrite_detected;
    uint8_t alive_counter;
} CAN_Protocol_LogHeartbeat_t;

uint16_t CAN_Protocol_ReadU16LE(const uint8_t *data);
uint32_t CAN_Protocol_ReadU32LE(const uint8_t *data);
void CAN_Protocol_WriteU32LE(uint8_t *data, uint32_t value);

uint8_t CAN_Protocol_IsValidId(uint8_t is_extended,
                               uint32_t identifier);

uint8_t CAN_Protocol_DecodeRtcAlarmCommand(
    const uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE],
    CAN_Protocol_RtcAlarmCommand_t *command);

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

void CAN_Protocol_EncodeLogHeartbeat(
    const CAN_Protocol_LogHeartbeat_t *heartbeat,
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE]);

#endif /* CAN_PROTOCOL_H */
