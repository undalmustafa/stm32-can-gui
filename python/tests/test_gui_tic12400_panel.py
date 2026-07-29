import os
import sys
from pathlib import Path


os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

try:
    from PySide6.QtWidgets import QApplication
except ImportError:
    print("SKIP: PySide6 is not installed")
    raise SystemExit(0)


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
GUI_DIRECTORY = PACKAGE_ROOT / "upload"
if not (GUI_DIRECTORY / "can_gui_app").is_dir():
    GUI_DIRECTORY = PACKAGE_ROOT
sys.path.insert(0, str(GUI_DIRECTORY))

from can_gui_app.tic12400_controller import Tic12400Controller  # noqa: E402
from can_gui_app.tic12400_panel import Tic12400Panel  # noqa: E402
from can_gui_app.protocol import (  # noqa: E402
    TIC12400_PROFILE_CONFIGURATION_VALID,
    TIC12400_PROFILE_RX_ID,
    TIC12400_STATUS_RX_ID,
    TIC12400_SWITCH_DATA_VALID,
    TIC12400_SWITCH_STATE_RX_ID,
)


class Message:
    def __init__(self, arbitration_id, data):
        self.arbitration_id = arbitration_id
        self.data = data


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    app = QApplication.instance() or QApplication([])
    polarity_requests = []
    panel = Tic12400Panel(
        polarity_requested=(
            lambda mask: polarity_requests.append(mask) or True
        )
    )
    controller = Tic12400Controller(
        renderer=panel.render,
        clock=lambda: 12.0,
    )
    controller.render()
    expect(panel.status_label.text() == "Waiting for switch data…",
           "panel starts without guessing switch states")

    controller.handle_message(Message(
        TIC12400_STATUS_RX_ID,
        [0x3F, 0x20, 0, 0, 0, 0, 8, 0],
    ))
    controller.handle_message(Message(
        TIC12400_PROFILE_RX_ID,
        [
            0,
            0,
            0,
            0xFF,
            0x03,
            0,
            1,
            TIC12400_PROFILE_CONFIGURATION_VALID,
        ],
    ))
    closed = (1 << 0) | (1 << 22)
    valid = 0xFFEFFF
    controller.handle_message(Message(
        TIC12400_SWITCH_STATE_RX_ID,
        [
            closed & 0xFF,
            (closed >> 8) & 0xFF,
            (closed >> 16) & 0xFF,
            valid & 0xFF,
            (valid >> 8) & 0xFF,
            (valid >> 16) & 0xFF,
            1,
            TIC12400_SWITCH_DATA_VALID,
        ],
    ))
    app.processEvents()

    expect(panel.status_label.text() == "Switch monitoring active",
           "healthy switch monitoring is shown")
    expect(panel.channel_table.columnCount() == 3,
           "switch table includes applied polarity")
    expect(panel.channel_table.item(0, 1).text() == "CLOSED",
           "closed input is rendered")
    expect(panel.channel_table.item(1, 1).text() == "OPEN",
           "open input is rendered")
    expect(panel.channel_table.item(12, 1).text() == "Not available",
           "IN12 is visibly unavailable")
    expect(panel.channel_table.item(22, 1).text() == "CLOSED",
           "high bitmap channels are rendered correctly")
    expect(panel._polarity_combos[0].currentText() == "− Ground",
           "configurable inputs render confirmed ground polarity")
    expect(
        panel.channel_table.item(10, 2).text() ==
        "− Ground (fixed)",
        "IN10-IN23 are visibly locked to ground polarity",
    )
    expect(panel.apply_polarity_button.isEnabled(),
           "polarity apply is enabled after profile confirmation")

    panel._polarity_combos[0].setCurrentIndex(1)
    panel.apply_polarity_button.click()
    app.processEvents()
    expect(polarity_requests == [1],
           "apply sends the selected battery-input mask")
    expect(
        panel.polarity_status_label.text() ==
        "Polarity requested — awaiting MCU confirmation",
        "GUI waits for firmware confirmation",
    )
    controller.handle_message(Message(
        TIC12400_PROFILE_RX_ID,
        [
            1,
            0,
            0,
            0xFF,
            0x03,
            0,
            2,
            TIC12400_PROFILE_CONFIGURATION_VALID,
        ],
    ))
    app.processEvents()
    expect(
        panel.polarity_status_label.text() ==
        "Polarity profile applied",
        "confirmed polarity clears the pending state",
    )

    controller.handle_message(Message(
        TIC12400_STATUS_RX_ID,
        [0x41, 0x20, 5, 1, 1, 0, 1, 0],
    ))
    app.processEvents()
    expect(panel.status_label.text() == "Switch module fault",
           "responding module fault is distinguished from offline recovery")
    expect(panel.channel_table.item(0, 1).text() == "Unavailable",
           "faulted module suppresses stale closed state")

    controller.handle_message(Message(
        TIC12400_STATUS_RX_ID,
        [0x40, 0x20, 5, 1, 3, 0, 1, 0],
    ))
    app.processEvents()
    expect(
        panel.status_label.text() ==
        "Switch module unavailable — retrying",
        "offline module reports automatic recovery attempts",
    )

    print("PASS: TIC12400 end-user open/closed rendering")


if __name__ == "__main__":
    main()
