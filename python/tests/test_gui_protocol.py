import sys
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
GUI_DIRECTORY = PACKAGE_ROOT / "upload"

if not (GUI_DIRECTORY / "can_gui_app").is_dir():
    GUI_DIRECTORY = PACKAGE_ROOT

sys.path.insert(0, str(GUI_DIRECTORY))

import can_protocol_generated as generated  # noqa: E402
from can_gui_app import protocol  # noqa: E402


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    expect(protocol.PROTOCOL_VERSION == 2,
           "fixed-ID command transport uses protocol version 2")
    expect(protocol.GUI_COMMAND_ID_EXT == 0x1894AABB,
           "all GUI commands use the fixed extended identifier")
    expect(protocol.DIAGNOSTIC_REQUEST_TX_ID == 0x7E0
           and protocol.DIAGNOSTIC_RESPONSE_RX_ID == 0x7E8,
           "physical UDS request and response identifiers are generated")
    expect(protocol.UDS_VERSION == 2
           and protocol.UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL == 0x10
           and protocol.UDS_SERVICE_READ_DATA_BY_IDENTIFIER == 0x22
           and protocol.UDS_SERVICE_ROUTINE_CONTROL == 0x31
           and protocol.UDS_SERVICE_REQUEST_DOWNLOAD == 0x34
           and protocol.UDS_SERVICE_TRANSFER_DATA == 0x36
           and protocol.UDS_SERVICE_REQUEST_TRANSFER_EXIT == 0x37
           and protocol.UDS_SERVICE_TESTER_PRESENT == 0x3E,
           "UDS diagnostic and download service contract is generated")
    expect(protocol.UDS_SESSION_DEFAULT == 0x01
           and protocol.UDS_SESSION_PROGRAMMING == 0x02
           and protocol.UDS_SESSION_EXTENDED_DIAGNOSTIC == 0x03
           and protocol.UDS_DID_PROTOCOL_INFO == 0xF100
           and protocol.UDS_DID_STARTUP_HEALTH == 0xF101
           and protocol.UDS_DID_RUNTIME_HEALTH == 0xF102
           and protocol.UDS_DID_RESET_REASON == 0xF103,
           "UDS sessions and read-only DIDs are generated")
    expect(protocol.UDS_ROUTINE_ERASE_INACTIVE_SLOT == 0xFF00
           and protocol.UDS_DOWNLOAD_MAX_BLOCK_LENGTH == 258,
           "inactive-slot download parameters are generated")
    expect(protocol.RTC_STATUS_RX_ID == 0x551,
           "RTC status ID remains unchanged")
    expect(protocol.RTC_TIME_RX_ID == 0x556,
           "RTC time ID remains unchanged")
    expect(protocol.SYSTEM_STATUS_RX_ID == 0x557,
           "system status ID remains unchanged")
    expect(protocol.PWM_SELF_TEST_STATUS_RX_ID == 0x55E,
           "PWM built-in-test status ID is defined")
    expect(protocol.PWM_SELF_TEST_RESULT_RX_ID == 0x55F,
           "PWM built-in-test result ID is defined")
    expect(protocol.TIC12400_STATUS_RX_ID == 0x552,
           "TIC12400 status ID is generated")
    expect(protocol.TIC12400_SWITCH_STATE_RX_ID == 0x554,
           "TIC12400 switch-state ID is generated")
    expect(protocol.TIC12400_PROFILE_RX_ID == 0x555,
           "TIC12400 applied-profile ID is generated")
    expect(protocol.CAN_RX_HEALTH_RX_ID == 0x560,
           "MCU CAN RX health ID is generated")
    expect(protocol.TIMING_SERVICE_RX_ID == 0x561
           and protocol.TIMING_ACK_LATENCY_RX_ID == 0x562,
           "firmware timing telemetry IDs are generated")
    expect(protocol.SYSTEM_REQUEST_SLOT_1 == 0x01
           and protocol.SYSTEM_REQUEST_PWM == 0x10,
           "system request flags are generated")
    expect(protocol.SYSTEM_PHYSICAL_DATA_VALID == 0x01
           and protocol.SYSTEM_PHYSICAL_IN3_CLOSED == 0x10,
           "physical permission flags are generated")
    expect(protocol.SYSTEM_OVERRIDE_SLOT_1_BLOCKED == 0x01
           and protocol.SYSTEM_OVERRIDE_PWM_BLOCKED == 0x10,
           "control override flags are generated")
    expect(protocol.PWM_CONTROL_REQUESTED == 0x01
           and protocol.PWM_CONTROL_SWITCH_DATA_VALID == 0x08,
           "PWM control-policy flags are generated")
    expect(generated.PWM_SELF_TEST_STATE_NAMES[0] == "Idle"
           and generated.PWM_SELF_TEST_STATE_NAMES[5] == "Firmware error",
           "PWM self-test state names are generated")
    expect(protocol.TIC12400_CHANNEL_COUNT == 24,
           "TIC12400 channel count is generated")
    expect(protocol.TIC12400_BATTERY_CAPABLE_MASK == 0x3FF,
           "TIC12400 IN0-IN9 capability mask is generated")
    expect(protocol.TIC12400_RESULT_NAMES[0x00] == "OK",
           "TIC12400 successful service result is named")
    expect(protocol.TIC12400_RESULT_NAMES[0x05] == "DEVICE_SPI_ERROR",
           "TIC12400 SPI service failure is named")
    expect(protocol.CMD_PWM_SELF_TEST == 0x41,
           "PWM built-in-test command is defined")
    expect(protocol.CMD_TIC12400_SET_POLARITY == 0x50,
           "TIC12400 polarity command is defined")
    expect(protocol.command_token([0x10, 2, 1, 0, 0, 0, 0, 0]) == 0x8C,
           "command ACK token uses CRC-8/SAE-J1850")
    expect(protocol.GUI_COMMAND_ID_EXT == generated.GUI_COMMAND_ID_EXT,
           "GUI imports its command ID from the generated protocol")
    expect(protocol.CMD_LOG_GET_INFO == generated.CMD_LOG_GET_INFO,
           "GUI imports log commands from the generated protocol")
    expect(protocol.STM32_LOG_RESPONSE_RX_ID == generated.LOG_RESPONSE_TX_ID,
           "GUI RX aliases use generated firmware TX identifiers")
    expect(protocol.RTC_STATUS_DEFINITIONS == generated.RTC_STATUS_DEFINITIONS,
           "GUI imports RTC status definitions from the generated protocol")
    expect(not hasattr(generated, "GUI_COMMAND_SEQUENCE_MASK"),
           "generated protocol no longer exposes dynamic command-ID fields")
    expect(protocol.COMMAND_ACK_RX_ID == generated.COMMAND_ACK_TX_ID,
           "GUI receives generated command acknowledgements")
    expect(protocol.RTC_ALARM_EVENT_RX_ID == 0x558,
           "RTC alarm event ID remains unchanged")
    expect(protocol.STM32_LOG_RESPONSE_RX_ID == 0x55A,
           "STM32 log response ID remains unchanged")
    expect(protocol.STM32_LOG_RECORD_MAGIC == 0x4C4F4731,
           "STM32 log magic remains unchanged")
    expect(protocol.STM32_LOG_COMMIT_MARKER == 0xA55A,
           "STM32 log commit marker remains unchanged")
    expect(protocol.STM32_LOG_INFO_FRAGMENT_COUNT == 3
           and protocol.STM32_LOG_RECORD_FRAGMENT_COUNT == 5,
           "STM32 log fragment counts are generated")
    expect(protocol.TIMING_SERVICE_NAMES == generated.TIMING_SERVICE_NAMES,
           "timing service names come from the generated protocol")
    expect(protocol.u16_to_le(0x1234) == [0x34, 0x12],
           "u16 encoding remains little-endian")
    expect(protocol.u32_to_le(0x12345678) == [0x78, 0x56, 0x34, 0x12],
           "u32 encoding remains little-endian")
    expect(protocol.parse_can_id("0x123") == 0x123,
           "hex CAN ID parsing remains unchanged")
    expect(protocol.decode_hal_status(1) == "HAL_ERROR",
           "HAL status decoding remains unchanged")
    expect(protocol.decode_i2c_error(0x00000004) == "AF/NACK",
           "I2C error decoding remains unchanged")
    expect(protocol.STM32_LOG_EVENT_NAMES[0x0205] == "RTC_RECOVERED",
           "RTC recovered event mapping is retained")
    expect(
        protocol.STM32_LOG_EVENT_NAMES[0x0108] ==
        "CAN_RX_BUDGET_EXHAUSTED",
        "firmware RX saturation event is named",
    )
    expect(
        protocol.decode_stm32_log_event_detail(0x0108, 3, 17) ==
        "NEW_HITS=3; TOTAL_HITS=17",
        "firmware RX saturation counts are decoded",
    )
    expect(
        protocol.STM32_LOG_EVENT_NAMES[0x010C] ==
        "CAN_RX_MESSAGE_LOST",
        "firmware RX loss event is named",
    )
    expect(
        protocol.decode_stm32_log_event_detail(0x010C, 2, 9) ==
        "NEW_EVENTS=2; TOTAL_EVENTS=9",
        "firmware RX loss counts are decoded",
    )
    expect(
        protocol.STM32_LOG_EVENT_NAMES[0x0109] ==
        "CAN_STARTUP_FAILED",
        "firmware degraded-startup event is named",
    )
    expect(
        protocol.decode_stm32_log_event_detail(0x0109, 4, 0x20) ==
        "STAGE=START_ERROR; FDCAN_ERROR=0x00000020",
        "firmware degraded-startup stage is decoded",
    )
    rx_health = protocol.decode_can_rx_health(
        [0x0F, 32, 0x34, 0x12, 0x78, 0x56, 0xBC, 0x9A]
    )
    expect(rx_health["message_lost"]
           and rx_health["budget_exhausted"],
           "CAN RX health flags decode")
    expect(rx_health["max_fifo_fill"] == 32
           and rx_health["message_lost_events"] == 0x1234
           and rx_health["fifo_full_events"] == 0x5678
           and rx_health["watermark_events"] == 0x9ABC,
           "CAN RX health counters decode little-endian")
    service_timing = protocol.decode_timing_service(
        [3, 0x07, 0x34, 0x12, 0x78, 0x56, 0xBC, 0x9A]
    )
    expect(service_timing == {
        "service_id": 3,
        "service_name": "RTC",
        "enabled": True,
        "current_overrun": True,
        "overrun_latched": True,
        "current_us": 0x1234,
        "minimum_us": 0x5678,
        "maximum_us": 0x9ABC,
    }, "service timing frame decodes flags and little-endian values")
    ack_timing = protocol.decode_timing_ack_latency(
        [100, 0, 0xE8, 0x03, 0x88, 0x13, 0x30, 0x75]
    )
    expect(ack_timing == {
        "p50_us": 100,
        "p95_us": 1000,
        "p99_us": 5000,
        "maximum_us": 30000,
    }, "ACK timing percentiles decode")
    expect(
        protocol.decode_stm32_log_event_detail(
            0x0006,
            3 | (2 << 8),
            12001,
        ) ==
        "SERVICE=RTC; NEW_OVERRUNS=2; MAX_US=12001",
        "timing overrun event detail decodes service and count",
    )
    expect(
        protocol.decode_stm32_log_event_detail(
            0x0500,
            3 | (9 << 8) | (10 << 16) | (1 << 24),
            1 << 2,
        ) ==
        "STATE=FAILED; PASSED=9/10; FAILED=1; "
        "FAILED_POINT_MASK=0x0004",
        "persistent PWM built-in-test summary is decoded",
    )
    expect(protocol.STM32_LOG_SOURCE_NAMES[5] == "PWM",
           "persistent PWM events use a dedicated source")

    print("PASS: GUI protocol constants and pure helpers")


if __name__ == "__main__":
    main()
