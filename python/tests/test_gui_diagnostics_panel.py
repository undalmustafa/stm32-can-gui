import sys
import types
from pathlib import Path


class FakeSignal:
    def __init__(self):
        self.callback = None

    def connect(self, callback):
        self.callback = callback


class FakeWidget:
    def __init__(self, text=""):
        self.text = text
        self.layout = None

    def setText(self, text):
        self.text = str(text)


class FakeButton(FakeWidget):
    def __init__(self, text=""):
        super().__init__(text)
        self.clicked = FakeSignal()

    def click(self):
        self.clicked.callback()


class FakeLayout:
    def __init__(self, parent=None):
        self.items = []
        if parent is not None:
            parent.layout = self

    def addWidget(self, widget, *_args):
        self.items.append(widget)


qtwidgets = types.ModuleType("PySide6.QtWidgets")
qtwidgets.QGridLayout = FakeLayout
qtwidgets.QGroupBox = FakeWidget
qtwidgets.QLabel = FakeWidget
qtwidgets.QPushButton = FakeButton
qtwidgets.QVBoxLayout = FakeLayout
pyside = types.ModuleType("PySide6")
pyside.QtWidgets = qtwidgets
sys.modules.setdefault("PySide6", pyside)
sys.modules.setdefault("PySide6.QtWidgets", qtwidgets)

PACKAGE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE_ROOT))

from can_gui_app.diagnostics_panel import DiagnosticsPanel  # noqa: E402
from can_gui_app.protocol import (  # noqa: E402
    UDS_DID_PROTOCOL_INFO,
    UDS_DID_RESET_REASON,
    UDS_DID_RUNTIME_HEALTH,
    UDS_DID_STARTUP_HEALTH,
)


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    refreshes = []
    panel = DiagnosticsPanel(lambda: refreshes.append(True))
    values = {
        UDS_DID_PROTOCOL_INFO: {
            "uds_version": 1, "protocol_version": 2, "log_version": 3,
            "isotp_capacity": 512, "request_id": 0x7E0,
            "response_id": 0x7E8,
        },
        UDS_DID_STARTUP_HEALTH: {
            "expected_mask": 0x0F, "ready_mask": 0x07,
            "failed_mask": 0x08, "first_failed_resource": 3,
            "first_failure_result": 5, "degraded": True,
        },
        UDS_DID_RUNTIME_HEALTH: {
            "uptime_ms": 12500, "latched_issue_flags": 1,
            "rejected_frames_total": 2, "can_rx_message_lost": 3,
            "can_tx_queue_overflow": 4, "isotp_protocol_errors": 5,
            "isotp_transport_failures": 6,
        },
        UDS_DID_RESET_REASON: {
            "decoded_flags": 0x0A, "raw_rsr": 0x1234,
            "capture_count": 1,
        },
    }
    panel.render("OK", "ECU diagnostics are live", values)
    expect(panel.status_label.text == "OK",
           "diagnostic connection state is visible")
    expect(panel.protocol_labels["CAN IDs"].text == "0x7E0 → 0x7E8",
           "generated diagnostic CAN IDs are rendered")
    expect(panel.startup_labels["State"].text == "DEGRADED",
           "startup degradation is prominent")
    expect(panel.runtime_labels["Uptime"].text == "12.5 s",
           "runtime uptime uses an operator-readable unit")
    expect("SOFTWARE_RESET" in panel.reset_labels["Decoded"].text,
           "decoded reset flags are rendered")

    refresh_button = panel.summary_group.layout.items[-1]
    refresh_button.click()
    expect(refreshes == [True], "manual refresh reaches the controller")

    print("PASS: live UDS diagnostics panel rendering")


if __name__ == "__main__":
    main()
