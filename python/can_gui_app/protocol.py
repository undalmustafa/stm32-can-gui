"""Wire-level constants and pure helpers for the STM32 CAN protocol."""

GUI_COMMAND_ID_EXT = 0x1894AABB

CMD_SET_SLOT_1 = 0x01
CMD_SET_SLOT_2 = 0x02
CMD_LED_CONTROL = 0x10
CMD_START_SLOT_1_COUNTER = 0x11
CMD_START_SLOT_2_COUNTER = 0x12
CMD_RTC_SET_TIME = 0x20
CMD_RTC_SET_DATETIME = 0x21
CMD_RTC_SET_ALARM = 0x22
CMD_LOG_GET_INFO = 0x30
CMD_LOG_READ_SEQUENCE = 0x31

SLOT_FLAG_ENABLE = 0x01
SLOT_FLAG_EXTENDED_ID = 0x02

RTC_ALARM_ENABLE_SECOND = 0x01
RTC_ALARM_ENABLE_MINUTE = 0x02
RTC_ALARM_ENABLE_HOUR = 0x04
RTC_ALARM_ENABLE_DAY = 0x08
RTC_ALARM_ENABLE_WEEKDAY = 0x10

RTC_STATUS_RX_ID = 0x551
RTC_TIME_RX_ID = 0x556
SYSTEM_STATUS_RX_ID = 0x557
RTC_ALARM_EVENT_RX_ID = 0x558
STM32_LOG_RESPONSE_RX_ID = 0x55A
STM32_LOG_HEARTBEAT_RX_ID = 0x55B

STM32_LOG_PROTOCOL_VERSION = 1
STM32_LOG_RECORD_SIZE = 32
STM32_LOG_RAM_CAPACITY = 64
STM32_LOG_RECORD_MAGIC = 0x4C4F4731
STM32_LOG_COMMIT_MARKER = 0xA55A
STM32_LOG_INFO_FRAGMENT_BASE = 0x70
STM32_LOG_RECORD_FRAGMENT_BASE = 0x80
STM32_LOG_ERROR_FRAME = 0xF0
STM32_LOG_HEARTBEAT_FLAG_READY = 0x01
STM32_LOG_HEARTBEAT_FLAG_OVERWRITE = 0x02
STM32_LOG_HEARTBEAT_FLAG_MASK = (
    STM32_LOG_HEARTBEAT_FLAG_READY |
    STM32_LOG_HEARTBEAT_FLAG_OVERWRITE
)

STM32_LOG_EVENT_SYSTEM_BOOT = 0x0001

STM32_RESET_REASON_FLAGS = (
    (0x00000001, "PIN_RESET"),
    (0x00000002, "POWER_ON_RESET"),
    (0x00000004, "BROWNOUT_RESET"),
    (0x00000008, "SOFTWARE_RESET"),
    (0x00000010, "IWDG_RESET"),
    (0x00000020, "WWDG_RESET"),
    (0x00000040, "LOW_POWER_RESET"),
)

STM32_RESET_REASON_KNOWN_MASK = sum(
    flag for flag, _name in STM32_RESET_REASON_FLAGS
)

COMMAND_NAMES = {
    CMD_SET_SLOT_1: "SET_SLOT_1",
    CMD_SET_SLOT_2: "SET_SLOT_2",
    CMD_LED_CONTROL: "LED_CONTROL",
    CMD_START_SLOT_1_COUNTER: "START_SLOT_1_COUNTER",
    CMD_START_SLOT_2_COUNTER: "START_SLOT_2_COUNTER",
    CMD_RTC_SET_TIME: "RTC_SET_TIME",
    CMD_RTC_SET_DATETIME: "RTC_SET_DATETIME",
    CMD_RTC_SET_ALARM: "RTC_SET_ALARM",
}

STM32_APPLICATION_RX_IDS = {
    RTC_STATUS_RX_ID,
    RTC_TIME_RX_ID,
    SYSTEM_STATUS_RX_ID,
    RTC_ALARM_EVENT_RX_ID,
    STM32_LOG_RESPONSE_RX_ID,
    STM32_LOG_HEARTBEAT_RX_ID,
}

STM32_LOG_EVENT_NAMES = {
    STM32_LOG_EVENT_SYSTEM_BOOT: "SYSTEM_BOOT",
    0x0100: "CAN_BUS_OFF",
    0x0101: "CAN_RECOVERY_OK",
    0x0102: "CAN_RECOVERY_FAILED",
    0x0103: "CAN_RX_REJECTED",
    0x0104: "CAN_TX_QUEUE_OVERFLOW",
    0x0105: "CAN_TX_QUEUE_STUCK",
    0x0106: "CAN_ERROR_PASSIVE",
    0x0200: "RTC_INIT_OK",
    0x0201: "RTC_INIT_FAILED",
    0x0202: "RTC_READ_FAILED",
    0x0203: "RTC_WRITE_OK",
    0x0204: "RTC_WRITE_FAILED",
    0x0205: "RTC_RECOVERED",
    0x0300: "ALARM_CONFIGURED",
    0x0301: "ALARM_TRIGGERED",
    0x0302: "ALARM_FAILED",
    0x0401: "TIMESTAMP_1",
    0x0402: "TIMESTAMP_2",
    0x0403: "TIMESTAMP_3",
    0x0404: "TIMESTAMP_4",
}


def decode_reset_reason_mask(reset_mask: int) -> str:
    """Decode the application reset-reason bit mask without losing bits."""
    reset_mask = int(reset_mask) & 0xFFFFFFFF
    reasons = [
        name
        for flag, name in STM32_RESET_REASON_FLAGS
        if reset_mask & flag
    ]
    unknown_bits = reset_mask & ~STM32_RESET_REASON_KNOWN_MASK

    if unknown_bits:
        reasons.append(f"UNKNOWN_BITS_0x{unknown_bits:08X}")

    return "+".join(reasons) if reasons else "NONE"


def decode_stm32_log_event_detail(event_code: int,
                                  data_0: int,
                                  data_1: int) -> str:
    """Return a human-readable interpretation while preserving raw fields."""
    if event_code == STM32_LOG_EVENT_SYSTEM_BOOT:
        return (
            f"RESET_CAUSE={decode_reset_reason_mask(data_0)}; "
            f"RAW_RSR=0x{int(data_1) & 0xFFFFFFFF:08X}"
        )

    return ""

STM32_LOG_SOURCE_NAMES = {
    0: "SYSTEM",
    1: "CAN",
    2: "RTC",
    3: "ALARM",
    4: "TIMESTAMP",
}

STM32_LOG_SEVERITY_NAMES = {
    0: "INFO",
    1: "WARNING",
    2: "FAULT",
}

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

RTC_COMMUNICATION_FAULT_CODES = {
    0xE1, 0xE2, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA
}


def u16_to_le(value: int):
    return [
        value & 0xFF,
        (value >> 8) & 0xFF,
    ]


def u32_to_le(value: int):
    return [
        value & 0xFF,
        (value >> 8) & 0xFF,
        (value >> 16) & 0xFF,
        (value >> 24) & 0xFF,
    ]


def parse_can_id(text: str) -> int:
    text = text.strip()

    if text.lower().startswith("0x"):
        return int(text, 16)


def decode_hal_status(status: int) -> str:
    status_names = {
        0: "HAL_OK",
        1: "HAL_ERROR",
        2: "HAL_BUSY",
        3: "HAL_TIMEOUT",
    }

    return status_names.get(status, f"HAL_UNKNOWN({status})")


def decode_i2c_error(error_mask: int) -> str:
    if error_mask == 0:
        return "NONE"

    error_definitions = [
        (0x00000001, "BERR"),
        (0x00000002, "ARLO"),
        (0x00000004, "AF/NACK"),
        (0x00000008, "OVR"),
        (0x00000010, "DMA"),
        (0x00000020, "TIMEOUT"),
        (0x00000040, "SIZE"),
        (0x00000080, "DMA_PARAM"),
        (0x00000100, "INVALID_CALLBACK"),
        (0x00000200, "INVALID_PARAM"),
    ]

    detected_errors = []
    known_mask = 0

    for mask, description in error_definitions:
        known_mask |= mask

        if error_mask & mask:
            detected_errors.append(description)

    unknown_bits = error_mask & ~known_mask

    if unknown_bits:
        detected_errors.append(f"UNKNOWN(0x{unknown_bits:08X})")

    return " | ".join(detected_errors)
