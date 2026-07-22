import sys
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
GUI_DIRECTORY = PACKAGE_ROOT / "upload"
if not (GUI_DIRECTORY / "can_gui_app").is_dir():
    GUI_DIRECTORY = PACKAGE_ROOT
sys.path.insert(0, str(GUI_DIRECTORY))

from can_gui_app.protocol import (  # noqa: E402
    CMD_PWM_CONFIG,
    PWM_FLAG_ENABLE,
    PWM_STATUS_RX_ID,
)
from can_gui_app.pwm_controller import PwmController  # noqa: E402


class FakeMessage:
    def __init__(self, arbitration_id, data):
        self.arbitration_id = arbitration_id
        self.data = bytes(data)


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    sent = []
    rendered = []
    controller = PwmController(
        command_sender=lambda data: sent.append(list(data)) or True,
        status_renderer=lambda **status: rendered.append(status),
    )

    controller.render_status()
    expect(rendered[-1]["state"] == "Waiting for STM32",
           "initial state waits for hardware confirmation")

    expect(controller.apply(True, 2000, 25.5),
           "valid PWM configuration is transmitted")
    expect(sent[-1] == [
        CMD_PWM_CONFIG, PWM_FLAG_ENABLE,
        0xD0, 0x07, 0x00, 0x00, 0xFF, 0x00,
    ], "controller encodes 2 kHz and 25.5-percent duty")
    expect(rendered[-1]["state"] == "Command sent",
           "transmission does not prematurely claim the output is running")

    expect(controller.handle_message(FakeMessage(
        PWM_STATUS_RX_ID,
        [1, 0xD0, 0x07, 0, 0, 0xFF, 0, 0],
    )), "PWM status frame is consumed")
    expect(rendered[-1] == {
        "state": "Running",
        "frequency_hz": 2000,
        "duty_percent": 25.5,
        "result": 0,
    }, "confirmed PWM state is rendered")

    expect(controller.handle_message(FakeMessage(PWM_STATUS_RX_ID, [1])),
           "short PWM status is consumed safely")
    expect(not controller.handle_message(FakeMessage(0x123, [0] * 8)),
           "unrelated frames remain available to other controllers")

    print("PASS: GUI PWM command and confirmed status")


if __name__ == "__main__":
    main()
