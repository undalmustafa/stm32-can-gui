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
        self.enabled = True
        self.stylesheet = ""
        self.tooltip = ""
        self.layout = None

    def setEnabled(self, enabled):
        self.enabled = enabled

    def setStyleSheet(self, stylesheet):
        self.stylesheet = stylesheet

    def setToolTip(self, tooltip):
        self.tooltip = tooltip

    def setWordWrap(self, _enabled):
        pass

    def setLayout(self, layout):
        self.layout = layout

    def setText(self, text):
        self._text = text

    def text(self):
        return self._text

    def setObjectName(self, name):
        self.object_name = name


class FakeSpinBox(FakeWidget):
    def __init__(self):
        super().__init__()
        self._value = 0
        self.minimum = None
        self.maximum = None
        self.valueChanged = FakeSignal()

    def setRange(self, minimum, maximum):
        self.minimum = minimum
        self.maximum = maximum

    def setValue(self, value):
        self._value = value
        self.valueChanged.emit(value)

    def value(self):
        return self._value


class FakeCheckBox(FakeWidget):
    def __init__(self, text=""):
        super().__init__(text)
        self._checked = False
        self.toggled = FakeSignal()

    def setChecked(self, checked):
        self._checked = checked
        self.toggled.emit(checked)

    def isChecked(self):
        return self._checked


class FakeComboBox(FakeWidget):
    def __init__(self):
        super().__init__()
        self.items = []
        self.index = 0

    def addItems(self, items):
        self.items.extend(items)

    def setCurrentIndex(self, index):
        self.index = index

    def currentIndex(self):
        return self.index


class FakeButton(FakeWidget):
    def __init__(self, text=""):
        super().__init__(text)
        self.clicked = FakeSignal()


class FakeLayout:
    def __init__(self, *_args):
        self.items = []

    def addWidget(self, widget, *_args):
        self.items.append(widget)

    def addLayout(self, layout):
        self.items.append(layout)

    def setContentsMargins(self, *_args):
        pass

    def setSpacing(self, _spacing):
        pass


qtwidgets = types.ModuleType("PySide6.QtWidgets")
qtwidgets.QCheckBox = FakeCheckBox
qtwidgets.QComboBox = FakeComboBox
qtwidgets.QGroupBox = FakeWidget
qtwidgets.QHBoxLayout = FakeLayout
qtwidgets.QLabel = FakeWidget
qtwidgets.QPushButton = FakeButton
qtwidgets.QSpinBox = FakeSpinBox
qtwidgets.QVBoxLayout = FakeLayout

pyside = types.ModuleType("PySide6")
pyside.QtWidgets = qtwidgets
sys.modules.setdefault("PySide6", pyside)
sys.modules.setdefault("PySide6.QtWidgets", qtwidgets)

from can_gui_app.rtc_panel import RtcPanel  # noqa: E402


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    requests = []
    panel = RtcPanel(
        set_datetime_requested=lambda: requests.append("datetime"),
        set_alarm_requested=lambda: requests.append("alarm"),
        disable_alarm_requested=lambda: requests.append("disable"),
    )

    expect(panel.get_log_time() == "",
           "unknown RTC values do not create a CSV RTC timestamp")
    panel.render_time("14:16:41.46", "17/07/2026", "Weekday: Cuma (5)",
                      "RX 0x556: 0E 10 29 2E 11 07 1A 05")
    expect(panel.get_log_time() == "17/07/2026 14:16:41.46",
           "valid rendered RTC values are exposed to the logger")

    panel.set_year_spin.setValue(2026)
    panel.set_month_spin.setValue(7)
    panel.set_day_spin.setValue(17)
    expect(panel.set_weekday_combo.currentIndex() == 5,
           "17 July 2026 maps to PCA2131 Friday index 5")
    expect(not panel.set_weekday_combo.enabled,
           "automatic weekday mode locks manual weekday selection")

    datetime_request = panel.get_datetime_request()
    expect(datetime_request["full_year"] == 2026,
           "calendar form exposes the four-digit year")
    expect(datetime_request["weekday"] == 5,
           "calendar form exposes the computed PCA2131 weekday")

    alarm_request = panel.get_alarm_request()
    expect(alarm_request["hour_enabled"],
           "hour comparison is enabled by default")
    expect(alarm_request["minute_enabled"],
           "minute comparison is enabled by default")
    expect(alarm_request["second_enabled"],
           "second comparison is enabled by default")
    expect(not alarm_request["day_enabled"],
           "day comparison is disabled by default")
    expect(not panel.alarm_day_spin.enabled,
           "disabled alarm fields are disabled in the form")

    panel.alarm_day_enable.setChecked(True)
    expect(panel.alarm_day_spin.enabled,
           "enabling an alarm comparison enables its value widget")

    panel.set_button.clicked.emit()
    panel.alarm_set_button.clicked.emit()
    panel.alarm_disable_button.clicked.emit()
    expect(requests == ["datetime", "alarm", "disable"],
           "RTC buttons preserve their application callbacks")

    panel.render_diagnostics(
        health_text="OK",
        health_color="#168018",
        link_text="I2C=OK",
        clock_text="CLOCK=VALID",
        calendar_state="VALID",
        event={
            "severity": "WARN",
            "code": "0xE2",
            "mnemonic": "READ_FAILED",
            "hal": "HAL_ERROR",
            "i2c": "AF/NACK",
            "error_mask": "0x00000004",
            "description": "RTC read failed",
        },
    )
    expect(panel.health_label.text() == "RTC is healthy and ready",
           "RTC health uses a plain-language summary")
    expect(panel.link_status_label.text() == "I2C connection\nConnected",
           "RTC I2C state is labeled for non-specialists")
    expect(panel.event_label.text() == "RTC read failed",
           "latest RTC event uses its human-readable description")
    expect("READ_FAILED" in panel.event_label.tooltip,
           "engineering RTC event details remain in the tooltip")

    panel.render_alarm("TRIGGERED", "17/07/2026 09:55:30", "#8A6D00",
                       "RX 0x558")
    expect(panel.alarm_badge.text() == "Triggered",
           "alarm state is presented as a clear badge")

    print("PASS: GUI RTC panel form state, callbacks and rendering")


if __name__ == "__main__":
    main()
