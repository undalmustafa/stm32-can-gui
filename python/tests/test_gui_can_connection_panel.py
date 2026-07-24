import sys
import types
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
GUI_DIRECTORY = PACKAGE_ROOT / "upload"
if not (GUI_DIRECTORY / "can_gui_app").is_dir():
    GUI_DIRECTORY = PACKAGE_ROOT
sys.path.insert(0, str(GUI_DIRECTORY))


class FakeSignal:
    def __init__(self):
        self.callbacks = []

    def connect(self, callback):
        self.callbacks.append(callback)

    def emit(self, *args):
        for callback in self.callbacks:
            callback(*args)


class FakeWidget:
    def __init__(self, text="", *_args, **_kwargs):
        self._text = text
        self.layout = None
        self.stylesheet = ""
        self.tooltip = ""

    def setText(self, text):
        self._text = text

    def text(self):
        return self._text

    def setLayout(self, layout):
        self.layout = layout

    def setStyleSheet(self, stylesheet):
        self.stylesheet = stylesheet

    def setToolTip(self, tooltip):
        self.tooltip = tooltip

    def setWordWrap(self, _enabled):
        pass

    def setObjectName(self, name):
        self.object_name = name

    def setMinimumWidth(self, width):
        self.minimum_width = width


class FakeButton(FakeWidget):
    def __init__(self, text=""):
        super().__init__(text)
        self.clicked = FakeSignal()


class FakeComboBox(FakeWidget):
    def __init__(self):
        super().__init__()
        self.items = []
        self.index = -1
        self.currentIndexChanged = FakeSignal()

    def addItem(self, label, data):
        self.items.append((label, data))
        if self.index < 0:
            self.index = 0

    def findData(self, data):
        for index, (_label, item_data) in enumerate(self.items):
            if item_data == data:
                return index
        return -1

    def setCurrentIndex(self, index):
        self.index = index

    def currentData(self):
        return self.items[self.index][1]


class FakeSpinBox(FakeWidget):
    def __init__(self):
        super().__init__()
        self._value = 0
        self.minimum = None
        self.maximum = None

    def setRange(self, minimum, maximum):
        self.minimum = minimum
        self.maximum = maximum

    def setValue(self, value):
        self._value = value

    def value(self):
        return self._value

    def setSuffix(self, suffix):
        self.suffix = suffix


class FakeLayout:
    def __init__(self, *_args):
        self.items = []

    def addWidget(self, widget, *_args):
        self.items.append(widget)

    def addLayout(self, layout):
        self.items.append(layout)

    def addSpacing(self, spacing):
        self.items.append(("spacing", spacing))

    def addStretch(self):
        self.items.append("stretch")

    def setContentsMargins(self, *_args):
        pass

    def setSpacing(self, _spacing):
        pass


qtwidgets = types.ModuleType("PySide6.QtWidgets")
qtwidgets.QGroupBox = FakeWidget
qtwidgets.QHBoxLayout = FakeLayout
qtwidgets.QLabel = FakeWidget
qtwidgets.QLineEdit = FakeWidget
qtwidgets.QComboBox = FakeComboBox
qtwidgets.QPushButton = FakeButton
qtwidgets.QSpinBox = FakeSpinBox
qtwidgets.QVBoxLayout = FakeLayout
qtwidgets.QWidget = FakeWidget

pyside = types.ModuleType("PySide6")
pyside.QtWidgets = qtwidgets
sys.modules.setdefault("PySide6", pyside)
sys.modules.setdefault("PySide6.QtWidgets", qtwidgets)

from can_gui_app.can_connection_panel import CanConnectionPanel  # noqa: E402


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    connection_requests = []
    panel = CanConnectionPanel(
        connect_requested=lambda **request: connection_requests.append(
            request
        ),
        platform="win32",
    )

    expect(panel.get_connection_request() == {
        "interface": "pcan",
        "channel": "PCAN_USBBUS1",
        "bitrate": 500000,
    }, "PCAN channel and bitrate defaults remain unchanged")

    panel.channel_input.setText("  PCAN_USBBUS2  ")
    panel.bitrate_input.setValue(250000)
    panel.connect_button.clicked.emit()
    expect(connection_requests == [{
        "interface": "pcan",
        "channel": "PCAN_USBBUS2",
        "bitrate": 250000,
    }], "Connect button forwards normalized channel and bitrate")

    panel.show_connected("pcan", "PCAN_USBBUS2", 250000)
    expect(
        panel.connection_status_label.text()
        == "Connected: pcan/PCAN_USBBUS2, 250000 bit/s",
        "successful connection status text remains unchanged",
    )
    expect("#168018" in panel.connection_status_label.stylesheet,
           "successful connection is rendered green")

    panel.show_disconnected()
    expect(panel.connection_status_label.text() == "Disconnected",
           "failed connection is rendered disconnected")
    expect("#C62828" in panel.connection_status_label.stylesheet,
           "failed connection is rendered red")

    panel.render_health(
        severity="WARN",
        code="BUS_HEAVY",
        detail="Driver=0x00000008",
        tooltip="PCAN reported BUSHEAVY",
        rx_count=351,
        error_event_count=2,
        rx_budget_hit_count=3,
        error_frame_count=4,
    )
    expect(
        panel.health_label.text() == "CAN bus has communication errors",
        "CAN warning is presented in plain language",
    )
    expect(panel.health_badge.text() == "Attention",
           "warning state has a recognizable badge")
    expect("351 frames" in panel.health_metrics_label.text(),
           "CAN counters are formatted as readable metrics")
    expect("RX poll budget hits: 3" in panel.health_label.tooltip,
           "engineering diagnostics remain available in the tooltip")
    expect("CAN error frames: 4" in panel.health_label.tooltip,
           "CAN error-frame diagnostics remain in the tooltip")

    linux_panel = CanConnectionPanel(
        connect_requested=lambda **_request: None,
        platform="linux",
    )
    expect(linux_panel.get_connection_request() == {
        "interface": "socketcan",
        "channel": "can0",
        "bitrate": 500000,
    }, "Linux defaults to SocketCAN can0")
    linux_panel.interface_input.setCurrentIndex(
        linux_panel.interface_input.findData("pcan")
    )
    linux_panel.interface_input.currentIndexChanged.emit(1)
    expect(linux_panel.channel_input.text() == "PCAN_USBBUS1",
           "changing backend updates its conventional channel default")

    panel.render_health(
        severity="OK",
        code="ACTIVE",
        detail="STM32 traffic active",
        tooltip="healthy",
        rx_count=400,
        error_event_count=2,
        rx_budget_hit_count=3,
        error_frame_count=4,
    )
    expect(panel.health_badge.text() == "Healthy",
           "healthy CAN state is immediately recognizable")
    expect(panel.health_label.text() == "Receiving data from STM32",
           "healthy CAN summary is user-facing")

    print("PASS: GUI CAN connection inputs and health rendering")


if __name__ == "__main__":
    main()
