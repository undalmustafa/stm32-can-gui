"""Wire-level constants and pure helpers for the STM32 CAN protocol."""

from can_protocol_generated import (
    CAN_RX_HEALTH_BUDGET_EXHAUSTED,
    CAN_RX_HEALTH_FIFO_FULL_SEEN,
    CAN_RX_HEALTH_HAL_ERROR,
    CAN_RX_HEALTH_MESSAGE_LOST,
    CAN_RX_HEALTH_TX_ID,
    CAN_RX_HEALTH_WATERMARK_SEEN,
    TIMING_ACK_LATENCY_TX_ID,
    TIMING_SERVICE_CURRENT_OVERRUN,
    TIMING_SERVICE_ENABLED,
    TIMING_SERVICE_NAMES,
    TIMING_SERVICE_OVERRUN_LATCHED,
    TIMING_SERVICE_TX_ID,
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
    CMD_TIC12400_SET_POLARITY,
    GUI_COMMAND_ID_EXT,
    INPUT_CAPTURE_STATUS_TX_ID,
    LOG_COMMIT_MARKER,
    LOG_ERROR_FRAME,
    LOG_HEARTBEAT_TX_ID,
    LOG_HEARTBEAT_FLAG_OVERWRITE,
    LOG_HEARTBEAT_FLAG_READY,
    LOG_INFO_FRAGMENT_BASE,
    LOG_INFO_FRAGMENT_COUNT,
    LOG_INFO_WIRE_SIZE,
    LOG_VERSION,
    LOG_RAM_CAPACITY,
    LOG_RECORD_FRAGMENT_BASE,
    LOG_RECORD_FRAGMENT_COUNT,
    LOG_RECORD_MAGIC,
    LOG_RECORD_SIZE,
    LOG_RESPONSE_TX_ID,
    COMMAND_ACK_TX_ID,
    PWM_SELF_TEST_RESULT_TX_ID,
    PWM_SELF_TEST_STATUS_TX_ID,
    PWM_CONTROL_BLOCKED,
    PWM_CONTROL_PHYSICAL_PERMITTED,
    PWM_CONTROL_REQUESTED,
    PWM_CONTROL_SWITCH_DATA_VALID,
    PWM_STATUS_TX_ID,
    PROTOCOL_VERSION,
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
    SYSTEM_OVERRIDE_LED_1_OVERRIDDEN,
    SYSTEM_OVERRIDE_PWM_BLOCKED,
    SYSTEM_OVERRIDE_SLOT_1_BLOCKED,
    SYSTEM_OVERRIDE_SLOT_2_BLOCKED,
    SYSTEM_PHYSICAL_DATA_VALID,
    SYSTEM_PHYSICAL_IN0_CLOSED,
    SYSTEM_PHYSICAL_IN1_CLOSED,
    SYSTEM_PHYSICAL_IN2_CLOSED,
    SYSTEM_PHYSICAL_IN3_CLOSED,
    SYSTEM_REQUEST_LED_1,
    SYSTEM_REQUEST_LED_2,
    SYSTEM_REQUEST_PWM,
    SYSTEM_REQUEST_SLOT_1,
    SYSTEM_REQUEST_SLOT_2,
    SYSTEM_STATUS_TX_ID,
    TIC12400_CHANNEL_COUNT,
    TIC12400_BATTERY_CAPABLE_MASK,
    TIC12400_PROFILE_CONFIGURATION_VALID,
    TIC12400_PROFILE_TX_ID,
    TIC12400_RESULT_NAMES,
    TIC12400_SWITCH_DATA_VALID,
    TIC12400_SWITCH_STATE_TX_ID,
    TIC12400_STATUS_CONFIGURATION_VALID,
    TIC12400_STATUS_CRC_COMPLETE,
    TIC12400_STATUS_MONITORING,
    TIC12400_STATUS_ONLINE,
    TIC12400_STATUS_POR_OBSERVED,
    TIC12400_STATUS_SERVICE_FAULT,
    TIC12400_STATUS_TX_ID,
    TIC12400_TRANSACTION_OTHER_INTERRUPT,
    TIC12400_TRANSACTION_PARITY_FAIL,
    TIC12400_TRANSACTION_POWER_ON_RESET,
    TIC12400_TRANSACTION_SPI_FAIL,
    TIC12400_TRANSACTION_SUPPLY_THRESHOLD,
    TIC12400_TRANSACTION_SWITCH_STATE_CHANGE,
    TIC12400_TRANSACTION_TEMPERATURE,
    COMMAND_ACK_ACCEPTED,
    COMMAND_ACK_ACCESS_DENIED,
    COMMAND_ACK_INVALID_PAYLOAD,
    COMMAND_ACK_UNKNOWN_COMMAND,
    COMMAND_ACK_FLAG_ACCESS_OPEN,
    COMMAND_ACK_FLAG_EXECUTED,
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
COMMAND_ACK_RX_ID = COMMAND_ACK_TX_ID
TIC12400_STATUS_RX_ID = TIC12400_STATUS_TX_ID
TIC12400_SWITCH_STATE_RX_ID = TIC12400_SWITCH_STATE_TX_ID
TIC12400_PROFILE_RX_ID = TIC12400_PROFILE_TX_ID
CAN_RX_HEALTH_RX_ID = CAN_RX_HEALTH_TX_ID
TIMING_SERVICE_RX_ID = TIMING_SERVICE_TX_ID
TIMING_ACK_LATENCY_RX_ID = TIMING_ACK_LATENCY_TX_ID

STM32_LOG_PROTOCOL_VERSION = LOG_VERSION
STM32_LOG_RECORD_SIZE = LOG_RECORD_SIZE
STM32_LOG_RAM_CAPACITY = LOG_RAM_CAPACITY
STM32_LOG_RECORD_MAGIC = LOG_RECORD_MAGIC
STM32_LOG_COMMIT_MARKER = LOG_COMMIT_MARKER
STM32_LOG_INFO_FRAGMENT_BASE = LOG_INFO_FRAGMENT_BASE
STM32_LOG_INFO_FRAGMENT_COUNT = LOG_INFO_FRAGMENT_COUNT
STM32_LOG_INFO_WIRE_SIZE = LOG_INFO_WIRE_SIZE
STM32_LOG_RECORD_FRAGMENT_BASE = LOG_RECORD_FRAGMENT_BASE
STM32_LOG_RECORD_FRAGMENT_COUNT = LOG_RECORD_FRAGMENT_COUNT
STM32_LOG_ERROR_FRAME = LOG_ERROR_FRAME
STM32_LOG_HEARTBEAT_FLAG_READY = LOG_HEARTBEAT_FLAG_READY
STM32_LOG_HEARTBEAT_FLAG_OVERWRITE = LOG_HEARTBEAT_FLAG_OVERWRITE
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
    CMD_TIC12400_SET_POLARITY: "TIC12400_SET_POLARITY",
}

COMMAND_ACK_STATUS_NAMES = {
    COMMAND_ACK_ACCEPTED: "ACCEPTED",
    COMMAND_ACK_INVALID_PAYLOAD: "INVALID_PAYLOAD",
    COMMAND_ACK_UNKNOWN_COMMAND: "UNKNOWN_COMMAND",
    COMMAND_ACK_ACCESS_DENIED: "ACCESS_DENIED",
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
    COMMAND_ACK_RX_ID,
    TIC12400_STATUS_RX_ID,
    TIC12400_SWITCH_STATE_RX_ID,
    TIC12400_PROFILE_RX_ID,
    CAN_RX_HEALTH_RX_ID,
    TIMING_SERVICE_RX_ID,
    TIMING_ACK_LATENCY_RX_ID,
}

STM32_LOG_EVENT_NAMES = {
    STM32_LOG_EVENT_SYSTEM_BOOT: "SYSTEM_BOOT",
    0x0006: "TIMING_OVERRUN",
    0x0100: "CAN_BUS_OFF",
    0x0101: "CAN_RECOVERY_OK",
    0x0102: "CAN_RECOVERY_FAILED",
    0x0103: "CAN_RX_REJECTED",
    0x0104: "CAN_TX_QUEUE_OVERFLOW",
    0x0105: "CAN_TX_QUEUE_STUCK",
    0x0106: "CAN_ERROR_PASSIVE",
    0x0107: "CAN_CONTROL_ACCESS_OPENED",
    0x0108: "CAN_RX_BUDGET_EXHAUSTED",
    0x0109: "CAN_STARTUP_FAILED",
    0x010A: "CAN_RX_FIFO_WATERMARK",
    0x010B: "CAN_RX_FIFO_FULL",
    0x010C: "CAN_RX_MESSAGE_LOST",
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
    0x0500: "PWM_SELF_TEST_COMPLETED",
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

    if event_code == 0x0006:
        service_id = int(data_0) & 0xFF
        new_overruns = (int(data_0) >> 8) & 0x00FFFFFF
        return (
            f"SERVICE={TIMING_SERVICE_NAMES.get(service_id, service_id)}; "
            f"NEW_OVERRUNS={new_overruns}; MAX_US={int(data_1)}"
        )

    if event_code == 0x0108:
        return f"NEW_HITS={int(data_0)}; TOTAL_HITS={int(data_1)}"

    if event_code in {0x010A, 0x010B, 0x010C}:
        return f"NEW_EVENTS={int(data_0)}; TOTAL_EVENTS={int(data_1)}"

    if event_code == 0x0109:
        stage_names = {
            1: "PERIPHERAL_UNAVAILABLE",
            2: "FILTER_ERROR",
            3: "GLOBAL_FILTER_ERROR",
            4: "START_ERROR",
            5: "NOTIFICATION_ERROR",
            6: "FIFO_MODE_ERROR",
            7: "FIFO_WATERMARK_ERROR",
        }
        stage = int(data_0)
        return (
            f"STAGE={stage_names.get(stage, f'UNKNOWN_{stage}')}; "
            f"FDCAN_ERROR=0x{int(data_1) & 0xFFFFFFFF:08X}"
        )

    if event_code == 0x0500:
        state_names = {
            2: "PASSED",
            3: "FAILED",
            4: "CANCELLED",
            5: "ERROR",
        }
        state = int(data_0) & 0xFF
        passed = (int(data_0) >> 8) & 0xFF
        total = (int(data_0) >> 16) & 0xFF
        failed = (int(data_0) >> 24) & 0xFF
        return (
            f"STATE={state_names.get(state, f'UNKNOWN_{state}')}; "
            f"PASSED={passed}/{total}; FAILED={failed}; "
            f"FAILED_POINT_MASK=0x{int(data_1) & 0xFFFF:04X}"
        )

    return ""


def decode_can_rx_health(payload):
    """Decode the MCU FDCAN RX FIFO health telemetry frame."""
    if len(payload) != 8:
        raise ValueError("CAN RX health payload must contain 8 bytes")

    data = [int(value) & 0xFF for value in payload]
    flags = data[0]
    return {
        "flags": flags,
        "watermark_seen": bool(flags & CAN_RX_HEALTH_WATERMARK_SEEN),
        "fifo_full_seen": bool(flags & CAN_RX_HEALTH_FIFO_FULL_SEEN),
        "message_lost": bool(flags & CAN_RX_HEALTH_MESSAGE_LOST),
        "budget_exhausted": bool(
            flags & CAN_RX_HEALTH_BUDGET_EXHAUSTED
        ),
        "hal_error": bool(flags & CAN_RX_HEALTH_HAL_ERROR),
        "max_fifo_fill": data[1],
        "message_lost_events": data[2] | (data[3] << 8),
        "fifo_full_events": data[4] | (data[5] << 8),
        "watermark_events": data[6] | (data[7] << 8),
    }


def decode_timing_service(payload):
    """Decode one multiplexed DWT service timing sample."""
    if len(payload) != 8:
        raise ValueError("Timing service payload must contain 8 bytes")

    data = [int(value) & 0xFF for value in payload]
    service_id = data[0]
    if service_id not in TIMING_SERVICE_NAMES:
        raise ValueError(f"Unknown timing service ID: {service_id}")

    flags = data[1]
    return {
        "service_id": service_id,
        "service_name": TIMING_SERVICE_NAMES[service_id],
        "enabled": bool(flags & TIMING_SERVICE_ENABLED),
        "current_overrun": bool(
            flags & TIMING_SERVICE_CURRENT_OVERRUN
        ),
        "overrun_latched": bool(
            flags & TIMING_SERVICE_OVERRUN_LATCHED
        ),
        "current_us": data[2] | (data[3] << 8),
        "minimum_us": data[4] | (data[5] << 8),
        "maximum_us": data[6] | (data[7] << 8),
    }


def decode_timing_ack_latency(payload):
    """Decode RX-to-ACK enqueue latency percentile telemetry."""
    if len(payload) != 8:
        raise ValueError("Timing ACK payload must contain 8 bytes")

    data = [int(value) & 0xFF for value in payload]
    return {
        "p50_us": data[0] | (data[1] << 8),
        "p95_us": data[2] | (data[3] << 8),
        "p99_us": data[4] | (data[5] << 8),
        "maximum_us": data[6] | (data[7] << 8),
    }

STM32_LOG_SOURCE_NAMES = {
    0: "SYSTEM",
    1: "CAN",
    2: "RTC",
    3: "ALARM",
    4: "TIMESTAMP",
    5: "PWM",
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


def command_token(payload):
    """Return the CRC-8/SAE-J1850 token echoed by command ACK frames."""
    if len(payload) != 8:
        raise ValueError("Command payload must contain exactly 8 bytes")

    crc = 0xFF
    for value in payload:
        crc ^= int(value) & 0xFF
        for _unused in range(8):
            crc = (((crc << 1) ^ 0x1D)
                   if crc & 0x80 else (crc << 1)) & 0xFF
    return crc ^ 0xFF


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
