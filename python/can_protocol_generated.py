# AUTO-GENERATED from can_protocol.yaml — DO NOT EDIT
# Generated on: 2026-07-24 09:11:05

PROTOCOL_VERSION = 1
PAYLOAD_SIZE = 8

# CAN Identifiers
GUI_COMMAND_ID_EXT = 0x1894AABB
RTC_STATUS_TX_ID = 0x551
RTC_TIME_TX_ID = 0x556
SYSTEM_STATUS_TX_ID = 0x557
RTC_ALARM_EVENT_TX_ID = 0x558
PWM_STATUS_TX_ID = 0x55C

# Command Codes
CMD_SET_SLOT_1 = 0x01
CMD_SET_SLOT_2 = 0x02
CMD_LED_CONTROL = 0x10
CMD_START_SLOT_1_COUNTER = 0x11
CMD_START_SLOT_2_COUNTER = 0x12
CMD_RTC_SET_TIME = 0x20
CMD_RTC_SET_DATETIME = 0x21
CMD_RTC_SET_ALARM = 0x22
CMD_PWM_SET = 0x40

# Slot Flags
SLOT_FLAG_ENABLE = 0x01
SLOT_FLAG_EXTENDED_ID = 0x02

# Alarm Enable Flags
RTC_ALARM_ENABLE_SECOND = 0x01
RTC_ALARM_ENABLE_MINUTE = 0x02
RTC_ALARM_ENABLE_HOUR = 0x04
RTC_ALARM_ENABLE_DAY = 0x08
RTC_ALARM_ENABLE_WEEKDAY = 0x10

# Alarm Event Flags
RTC_ALARM_EVENT_AF = 0x01
RTC_ALARM_EVENT_AIE = 0x02
RTC_ALARM_EVENT_CONFIG_OK = 0x04

# RTC Status Definitions
RTC_STATUS_DEFINITIONS = {
    0xA1: ("INFO", "INIT_OK", "PCA2131 address probe acknowledged"),
    0xA2: ("INFO", "WRITE_VERIFY_OK", "Calendar write verified by readback"),
    0xA3: ("INFO", "LINK_RECOVERED", "I2C communication restored"),
    0xA4: ("INFO", "ALARM_WRITE_VERIFY_OK", "Alarm configuration verified by readback"),
    0xE1: ("FAULT", "INIT_FAILED", "PCA2131 not detected during initialization"),
    0xE2: ("FAULT", "READ_FAILED", "RTC register read failed"),
    0xE4: ("WARN", "INVALID_DATETIME", "Date/time parameters rejected"),
    0xE5: ("FAULT", "CONTROL_READ_FAILED", "Control_1 register read failed"),
    0xE6: ("FAULT", "STOP_ASSERT_FAILED", "STOP bit could not be asserted"),
    0xE7: ("FAULT", "PRESCALER_RESET_FAILED", "Prescaler reset command failed"),
    0xE8: ("FAULT", "CALENDAR_WRITE_FAILED", "Calendar register write failed"),
    0xE9: ("FAULT", "STOP_RELEASE_FAILED", "STOP bit could not be released"),
    0xEA: ("FAULT", "RECOVERY_FAILED", "RTC write recovery failed"),
    0xEB: ("WARN", "BUSY", "RTC operation already in progress"),
    0xEC: ("WARN", "ALARM_NOT_READY", "RTC is not ready for alarm configuration"),
    0xED: ("WARN", "INVALID_ALARM_CONFIG", "Alarm configuration was rejected"),
    0xEE: ("FAULT", "ALARM_WRITE_FAILED", "Alarm register write failed"),
    0xEF: ("FAULT", "ALARM_READBACK_FAILED", "Alarm register readback failed"),
    0xF0: ("FAULT", "ALARM_VERIFY_MISMATCH", "Alarm write/readback mismatch"),
    0xF1: ("FAULT", "ALARM_FLAG_CLEAR_FAILED", "Previous alarm flag could not be cleared"),
}

# RTC Communication Fault Codes
RTC_COMMUNICATION_FAULT_CODES = {
    0xE1, 0xE2, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA
}

