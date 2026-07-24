import sys
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
GUI_DIRECTORY = PACKAGE_ROOT / "upload"

if not (GUI_DIRECTORY / "can_gui_app").is_dir():
    GUI_DIRECTORY = PACKAGE_ROOT

sys.path.insert(0, str(GUI_DIRECTORY))

from can_gui_app import protocol  # noqa: E402


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    expect(protocol.GUI_COMMAND_ID_EXT == 0x1894AABB,
           "GUI command ID remains unchanged")
    expect(protocol.RTC_STATUS_RX_ID == 0x551,
           "RTC status ID remains unchanged")
    expect(protocol.RTC_TIME_RX_ID == 0x556,
           "RTC time ID remains unchanged")
    expect(protocol.SYSTEM_STATUS_RX_ID == 0x557,
           "system status ID remains unchanged")
    expect(protocol.RTC_ALARM_EVENT_RX_ID == 0x558,
           "RTC alarm event ID remains unchanged")
    expect(protocol.STM32_LOG_RESPONSE_RX_ID == 0x55A,
           "STM32 log response ID remains unchanged")
    expect(protocol.STM32_LOG_RECORD_MAGIC == 0x4C4F4731,
           "STM32 log magic remains unchanged")
    expect(protocol.STM32_LOG_COMMIT_MARKER == 0xA55A,
           "STM32 log commit marker remains unchanged")
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

    print("PASS: GUI protocol constants and pure helpers")


if __name__ == "__main__":
    main()
