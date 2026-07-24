#ifndef APP_LOG_H
#define APP_LOG_H

#include <stdint.h>

#define APP_LOG_RAM_CAPACITY   64U
#define APP_LOG_RECORD_MAGIC   0x4C4F4731UL
#define APP_LOG_COMMIT_MARKER  0xA55AU

typedef enum
{
    APP_LOG_SOURCE_SYSTEM = 0U,
    APP_LOG_SOURCE_CAN,
    APP_LOG_SOURCE_RTC,
    APP_LOG_SOURCE_ALARM,
    APP_LOG_SOURCE_TIMESTAMP
} App_Log_Source_t;

typedef enum
{
    APP_LOG_SEVERITY_INFO = 0U,
    APP_LOG_SEVERITY_WARNING,
    APP_LOG_SEVERITY_FAULT
} App_Log_Severity_t;

typedef enum
{
    APP_LOG_EVENT_SYSTEM_BOOT = 0x0001U,
    APP_LOG_EVENT_WATCHDOG_RESET_EVIDENCE = 0x0002U,

    APP_LOG_EVENT_CAN_BUS_OFF = 0x0100U,
    APP_LOG_EVENT_CAN_RECOVERY_OK = 0x0101U,
    APP_LOG_EVENT_CAN_RECOVERY_FAILED = 0x0102U,
    APP_LOG_EVENT_CAN_RX_REJECTED = 0x0103U,
    APP_LOG_EVENT_CAN_TX_QUEUE_OVERFLOW = 0x0104U,
    APP_LOG_EVENT_CAN_TX_QUEUE_STUCK = 0x0105U,
    APP_LOG_EVENT_CAN_ERROR_PASSIVE = 0x0106U,

    APP_LOG_EVENT_RTC_INIT_OK = 0x0200U,
    APP_LOG_EVENT_RTC_INIT_FAILED = 0x0201U,
    APP_LOG_EVENT_RTC_READ_FAILED = 0x0202U,
    APP_LOG_EVENT_RTC_WRITE_OK = 0x0203U,
    APP_LOG_EVENT_RTC_WRITE_FAILED = 0x0204U,
    APP_LOG_EVENT_RTC_RECOVERED = 0x0205U,

    APP_LOG_EVENT_ALARM_CONFIGURED = 0x0300U,
    APP_LOG_EVENT_ALARM_TRIGGERED = 0x0301U,
    APP_LOG_EVENT_ALARM_FAILED = 0x0302U,

    APP_LOG_EVENT_TIMESTAMP_1 = 0x0401U,
    APP_LOG_EVENT_TIMESTAMP_2 = 0x0402U,
    APP_LOG_EVENT_TIMESTAMP_3 = 0x0403U,
    APP_LOG_EVENT_TIMESTAMP_4 = 0x0404U
} App_Log_EventCode_t;

/*
 * Fixed 32-byte format. It is intentionally aligned to two STM32H7A3
 * 128-bit Flash programming units for the future persistent backend.
 */
typedef struct
{
    uint32_t magic;
    uint32_t sequence;
    uint32_t uptime_ms;
    uint32_t rtc_epoch_s;
    uint16_t event_code;
    uint8_t source;
    uint8_t severity;
    uint32_t data_0;
    uint32_t data_1;
    uint16_t crc16;
    uint16_t commit_marker;
} App_Log_Record_t;

typedef struct
{
    uint32_t write_count;
    uint32_t overwrite_count;
    uint32_t rejected_count;
    uint32_t next_sequence;
    uint32_t last_sequence;
    uint16_t count;
    uint16_t head_index;
    uint16_t tail_index;
    uint16_t last_event_code;
} App_Log_Debug_t;

typedef struct
{
    uint32_t write_count;
    uint32_t overwrite_count;
    uint32_t oldest_sequence;
    uint32_t latest_sequence;
    uint16_t count;
    uint16_t capacity;
    uint16_t record_size;
} App_Log_Info_t;

/* Exposed for debugger inspection. Application code must use the API. */
extern App_Log_Record_t g_appLogRecords[APP_LOG_RAM_CAPACITY];
extern volatile App_Log_Debug_t g_appLogDebug;

void App_Log_Init(void);

uint8_t App_Log_Push(App_Log_Source_t source,
                     App_Log_Severity_t severity,
                     App_Log_EventCode_t event_code,
                     uint32_t data_0,
                     uint32_t data_1);

uint8_t App_Log_PushWithRtcTime(App_Log_Source_t source,
                                App_Log_Severity_t severity,
                                App_Log_EventCode_t event_code,
                                uint32_t rtc_epoch_s,
                                uint32_t data_0,
                                uint32_t data_1);

uint16_t App_Log_GetCount(void);
uint8_t App_Log_ReadOldest(uint16_t age_index,
                          App_Log_Record_t *record);
uint8_t App_Log_ReadBySequence(uint32_t sequence,
                              App_Log_Record_t *record);
uint8_t App_Log_GetLatest(App_Log_Record_t *record);
uint8_t App_Log_IsRecordValid(const App_Log_Record_t *record);
void App_Log_GetInfo(App_Log_Info_t *info);
void App_Log_GetDebug(App_Log_Debug_t *debug);
void App_Log_Clear(void);

#endif /* APP_LOG_H */
