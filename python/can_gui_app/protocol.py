"""Wire-level constants and pure helpers for the STM32 CAN protocol."""

from can_protocol_generated import (
    CMD_LED_CONTROL,
    CMD_LOG_GET_INFO,
    CMD_LOG_READ_SEQUENCE,
    CMD_PWM_SELF_TEST,
    CMD_PWM_SET,
    CMD_RTC_SET_ALARM,
    CMD_RTC_SET_DATETIME,
    CMD_RTC_SET_TIME,
    CMD_SET_SLOT_1,
    CMD_SET_SLOT_2,
    CMD_START_SLOT_1_COUNTER,
    CMD_START_SLOT_2_COUNTER,
    GUI_COMMAND_ID_EXT,
    INPUT_CAPTURE_STATUS_TX_ID,
    LOG_HEARTBEAT_TX_ID,
    LOG_RESPONSE_TX_ID,
    PWM_SELF_TEST_RESULT_TX_ID,
    PWM_SELF_TEST_STATUS_TX_ID,
    PWM_STATUS_TX_ID,
    RTC_ALARM_ENABLE_DAY,
    RTC_ALARM_ENABLE_HOUR,
    RTC_ALARM_ENABLE_MINUTE,
    RTC_ALARM_ENABLE_SECOND,
    RTC_ALARM_ENABLE_WEEKDAY,
    RTC_ALARM_EVENT_TX_ID,
    RTC_COMMUNICATION_FAULT_CODES,
    RTC_STATUS_DEFINITIONS,
    RTC_STATUS_TX_ID,
    RTC_TIME_TX_ID,
    SLOT_FLAG_ENABLE,
    SLOT_FLAG_EXTENDED_ID,
    SYSTEM_STATUS_TX_ID,
)

RTC_STATUS_RX_ID = RTC_STATUS_TX_ID
RTC_TIME_RX_ID = RTC_TIME_TX_ID
SYSTEM_STATUS_RX_ID = SYSTEM_STATUS_TX_ID
RTC_ALARM_EVENT_RX_ID = RTC_ALARM_EVENT_TX_ID
STM32_LOG_RESPONSE_RX_ID = LOG_RESPONSE_TX_ID
STM32_LOG_HEARTBEAT_RX_ID = LOG_HEARTBEAT_TX_ID
PWM_STATUS_RX_ID = PWM_STATUS_TX_ID
INPUT_CAPTURE_STATUS_RX_ID = INPUT_CAPTURE_STATUS_TX_ID
PWM_SELF_TEST_STATUS_RX_ID = PWM_SELF_TEST_STATUS_TX_ID
PWM_SELF_TEST_RESULT_RX_ID = PWM_SELF_TEST_RESULT_TX_ID

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
    CMD_LOG_GET_INFO: "LOG_GET_INFO",
    CMD_LOG_READ_SEQUENCE: "LOG_READ_SEQUENCE",
    CMD_PWM_SET: "PWM_SET",
    CMD_PWM_SELF_TEST: "PWM_SELF_TEST",
}

STM32_APPLICATION_RX_IDS = {
    RTC_STATUS_RX_ID,
    RTC_TIME_RX_ID,
    SYSTEM_STATUS_RX_ID,
    RTC_ALARM_EVENT_RX_ID,
    STM32_LOG_RESPONSE_RX_ID,
    STM32_LOG_HEARTBEAT_RX_ID,
    PWM_STATUS_RX_ID,
    INPUT_CAPTURE_STATUS_RX_ID,
    PWM_SELF_TEST_STATUS_RX_ID,
    PWM_SELF_TEST_RESULT_RX_ID,
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
