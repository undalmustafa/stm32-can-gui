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

    def setText(self, text):
        self._text = text

    def text(self):
        return self._text

    def setLayout(self, layout):
        self.layout = layout

    def setStyleSheet(self, stylesheet):
        self.stylesheet = stylesheet

    def setPlaceholderText(self, _text):
        pass


class FakeButton(FakeWidget):
    def __init__(self, text=""):
        super().__init__(text)
        self.clicked = FakeSignal()


class FakeComboBox(FakeWidget):
    def __init__(self):
        super().__init__()
        self.items = []
        self.current_text = ""

    def addItems(self, items):
        self.items.extend(items)
        if items and not self.current_text:
            self.current_text = items[0]

    def setCurrentText(self, text):
        self.current_text = text

    def currentText(self):
        return self.current_text


class FakeSpinBox(FakeWidget):
    def __init__(self):
        super().__init__()
        self._value = 0
        self.minimum = None
        self.maximum = None
        self.suffix = ""

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

    def addWidget(self, widget):
        self.items.append(widget)

    def addLayout(self, layout):
        self.items.append(layout)


qtwidgets = types.ModuleType("PySide6.QtWidgets")
qtwidgets.QComboBox = FakeComboBox
qtwidgets.QGroupBox = FakeWidget
qtwidgets.QHBoxLayout = FakeLayout
qtwidgets.QLabel = FakeWidget
qtwidgets.QLineEdit = FakeWidget
qtwidgets.QPushButton = FakeButton
qtwidgets.QSpinBox = FakeSpinBox
qtwidgets.QVBoxLayout = FakeLayout

pyside = types.ModuleType("PySide6")
pyside.QtWidgets = qtwidgets
sys.modules.setdefault("PySide6", pyside)
sys.modules.setdefault("PySide6.QtWidgets", qtwidgets)

from can_gui_app.can_app_panel import CanAppPanel  # noqa: E402


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    slot_requests = []
    led_requests = []
    panel = CanAppPanel(
        slot_start_requested=lambda **request: slot_requests.append(request),
        led_command_requested=lambda led, state: led_requests.append(
            (led, state)
        ),
    )

    slot1 = panel.get_slot_request(panel.slot1)
    expect(slot1 == {
        "slot_no": 1,
        "can_id_text": "0x123",
        "id_type_text": "Standard",
        "cycle_time": 50,
        "counter": 100,
    }, "Slot 1 defaults remain unchanged")

    slot2 = panel.get_slot_request(panel.slot2)
    expect(slot2 == {
        "slot_no": 2,
        "can_id_text": "0x18FF50E5",
        "id_type_text": "Extended",
        "cycle_time": 50,
        "counter": 200,
    }, "Slot 2 defaults remain unchanged")

    panel.slot1.set_button.clicked.emit()
    panel.slot2.set_button.clicked.emit()
    expect(slot_requests == [slot1, slot2],
           "slot buttons forward complete form values")

    panel.led1_on_button.clicked.emit()
    panel.led1_off_button.clicked.emit()
    panel.led2_on_button.clicked.emit()
    panel.led2_off_button.clicked.emit()
    expect(led_requests == [(1, 1), (1, 0), (2, 1), (2, 0)],
           "all four LED command mappings remain unchanged")

    panel.render_status(
        slot_status={
            1: {
                "can_id": "0x123",
                "id_type": "Standard",
                "cycle_time": "50 ms",
                "counter": "100",
                "state": "Running",
            },
            2: {
                "can_id": "0x18FF50E5",
                "id_type": "Extended",
                "cycle_time": "50 ms",
                "counter": "200",
                "state": "Stopped",
            },
        },
        led_status={1: "ON", 2: "OFF"},
    )
    expect("CAN ID     : 0x123" in panel.slot1_status_label.text(),
           "Slot 1 status retains its engineering layout")
    expect("State      : Stopped" in panel.slot2_status_label.text(),
           "Slot 2 state is rendered")
    expect(panel.led_status_label.text() == "LED1 : ON\nLED2 : OFF",
           "LED status text remains unchanged")

    print("PASS: GUI CAN application panel slot/LED inputs and status")


if __name__ == "__main__":
    main()
