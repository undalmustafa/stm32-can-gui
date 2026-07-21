import sys
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
GUI_DIRECTORY = PACKAGE_ROOT / "upload"

if not (GUI_DIRECTORY / "can_gui_app").is_dir():
    GUI_DIRECTORY = PACKAGE_ROOT

sys.path.insert(0, str(GUI_DIRECTORY))

from can_gui_app.protocol import (  # noqa: E402
    CMD_RTC_SET_ALARM,
    CMD_RTC_SET_DATETIME,
    RTC_ALARM_ENABLE_HOUR,
    RTC_ALARM_ENABLE_MINUTE,
    RTC_ALARM_ENABLE_SECOND,
    RTC_ALARM_EVENT_RX_ID,
    RTC_STATUS_RX_ID,
    RTC_TIME_RX_ID,
)
from can_gui_app.rtc_controller import RtcController  # noqa: E402


class FakeMessage:
    def __init__(self, arbitration_id, data):
        self.arbitration_id = arbitration_id
        self.data = bytes(data)


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    sent_commands = []
    events = []
    diagnostics = []
    time_views = []
    alarm_views = []

    controller = RtcController(
        command_sender=lambda data: sent_commands.append(list(data)) or True,
        event_writer=lambda **event: events.append(event),
        diagnostics_renderer=lambda **view: diagnostics.append(view),
        time_renderer=lambda **view: time_views.append(view),
        alarm_renderer=lambda **view: alarm_views.append(view),
    )

    handled = controller.handle_message(FakeMessage(
        RTC_TIME_RX_ID,
        [10, 24, 36, 64, 16, 7, 26, 0x64],
    ))
    expect(handled, "0x556 is owned by the RTC controller")
    expect(time_views[-1]["time_text"] == "10:24:36.64",
           "RTC time formatting is preserved")
    expect(time_views[-1]["date_text"] == "16/07/2026",
           "RTC date formatting is preserved")
    expect(time_views[-1]["weekday_text"] == "Weekday: Perşembe (4)",
           "PCA2131 weekday mapping is preserved")
    expect(diagnostics[-1]["health_text"] == "OK",
           "ready valid calendar with OSF=0 is healthy")

    controller.handle_message(FakeMessage(
        RTC_STATUS_RX_ID,
        [0xE2, 1, 4, 0, 0, 0, 0, 0],
    ))
    expect(controller.link_ok is False, "RTC read failure marks I2C fault")
    expect(controller.calendar_state == "STALE",
           "communication failure marks the calendar stale")
    expect(events[-1]["event_code"] == "0xE2_READ_FAILED",
           "engineering RTC status mnemonic is retained")
    expect("HAL=HAL_ERROR" in events[-1]["detail"],
           "HAL result is decoded in the RTC log")
    expect("I2C=AF/NACK" in events[-1]["detail"],
           "I2C error mask is decoded in the RTC log")

    sent, weekday = controller.send_datetime(
        hundredth=46,
        second=41,
        minute=16,
        hour=14,
        day=17,
        month=7,
        full_year=2026,
        auto_weekday=True,
        weekday=0,
    )
    expect(sent, "valid datetime command is sent")
    expect(weekday == 5, "17 July 2026 maps to PCA2131 Friday index 5")
    expect(sent_commands[-1] == [
        CMD_RTC_SET_DATETIME, 46, 41, 16, 14, 17, 0xA7, 26
    ], "datetime command payload remains byte-compatible")
    expect(controller.last_event["mnemonic"] == "RTC_SET_DATETIME",
           "datetime command remains visible in RTC diagnostics")

    result = controller.send_alarm(
        second_enabled=True,
        minute_enabled=True,
        hour_enabled=True,
        day_enabled=False,
        weekday_enabled=False,
        second=30,
        minute=55,
        hour=9,
        day=16,
        weekday=4,
    )
    expected_mask = (
        RTC_ALARM_ENABLE_SECOND
        | RTC_ALARM_ENABLE_MINUTE
        | RTC_ALARM_ENABLE_HOUR
    )
    expect(result == "SENT", "enabled alarm fields are accepted")
    expect(sent_commands[-1] == [
        CMD_RTC_SET_ALARM, expected_mask, 30, 55, 9, 0, 0, 0
    ], "alarm command payload remains byte-compatible")
    expect(controller.alarm_pending_action == "SET",
           "alarm write waits for STM32 verification")
    expect(alarm_views[-1]["state"] == "PENDING",
           "alarm view reports pending verification")

    command_count = len(sent_commands)
    result = controller.send_alarm(
        second_enabled=False,
        minute_enabled=False,
        hour_enabled=False,
        day_enabled=False,
        weekday_enabled=False,
        second=0,
        minute=0,
        hour=0,
        day=1,
        weekday=0,
    )
    expect(result == "NO_FIELDS", "empty alarm selection is rejected")
    expect(len(sent_commands) == command_count,
           "empty alarm selection does not transmit")

    expect(controller.disable_alarm(), "alarm disable command is sent")
    expect(sent_commands[-1] == [CMD_RTC_SET_ALARM, 0, 0, 0, 0, 0, 0, 0],
           "alarm disable clears all comparison fields")
    expect(controller.alarm_pending_action == "DISABLE",
           "disable waits for STM32 verification")

    controller.handle_message(FakeMessage(
        RTC_STATUS_RX_ID,
        [0xA4, 0, 0, 0, 0, 0, 0, 0],
    ))
    expect(alarm_views[-1]["state"] == "DISABLED",
           "verified disable is shown as disabled")
    expect(controller.alarm_pending_action is None,
           "verified alarm command clears pending action")

    controller.handle_message(FakeMessage(
        RTC_ALARM_EVENT_RX_ID,
        [1, 7, 9, 55, 30, 17, 7, 26],
    ))
    expect(alarm_views[-1]["state"] == "TRIGGERED",
           "0x558 alarm event is rendered")
    expect(events[-1]["event_code"] == "TRIGGERED",
           "alarm trigger is written to the event log")
    expect("RX 0x558" in alarm_views[-1]["tooltip"],
           "raw alarm event remains available in the tooltip")

    expect(not controller.handle_message(FakeMessage(0x123, [0] * 8)),
           "unrelated CAN messages remain outside the RTC controller")

    print("PASS: GUI RTC controller protocol, diagnostics and alarm state")


if __name__ == "__main__":
    main()
