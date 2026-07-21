import csv
import sys
import tempfile
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
        self.stylesheet = ""
        self.tooltip = ""
        self.layout = None
        self.read_only = False

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

    def setReadOnly(self, read_only):
        self.read_only = read_only


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


class FakeButton(FakeWidget):
    def __init__(self, text=""):
        super().__init__(text)
        self.clicked = FakeSignal()


class FakeLayout:
    def __init__(self, *_args):
        self.items = []

    def addWidget(self, widget):
        self.items.append(widget)

    def addLayout(self, layout):
        self.items.append(layout)

    def setContentsMargins(self, *_args):
        pass


class FakeFileDialog:
    @staticmethod
    def getExistingDirectory(*_args):
        return ""


qtwidgets = types.ModuleType("PySide6.QtWidgets")
qtwidgets.QCheckBox = FakeCheckBox
qtwidgets.QFileDialog = FakeFileDialog
qtwidgets.QGroupBox = FakeWidget
qtwidgets.QHBoxLayout = FakeLayout
qtwidgets.QLabel = FakeWidget
qtwidgets.QLineEdit = FakeWidget
qtwidgets.QPushButton = FakeButton
qtwidgets.QVBoxLayout = FakeLayout

pyside = types.ModuleType("PySide6")
pyside.QtWidgets = qtwidgets
sys.modules.setdefault("PySide6", pyside)
sys.modules.setdefault("PySide6.QtWidgets", qtwidgets)

fake_can = types.ModuleType("can")
fake_can.Message = object
sys.modules.setdefault("can", fake_can)

from can_gui_app.event_log_panel import EventLogPanel  # noqa: E402


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def read_rows(path):
    with path.open("r", encoding="utf-8-sig", newline="") as csv_file:
        return list(csv.DictReader(csv_file))


def main():
    with tempfile.TemporaryDirectory() as temporary_directory:
        root = Path(temporary_directory)
        initial_directory = root / "initial"
        selected_directory = root / "selected"
        bus_holder = {"bus": None}

        panel = EventLogPanel(
            default_directory=initial_directory,
            bus_provider=lambda: bus_holder["bus"],
            rtc_time_provider=lambda: "17/07/2026 14:16:41.46",
            directory_selector=lambda _current: str(selected_directory),
        )

        expect("LOG  ENABLED" in panel.event_status_label.text(),
               "CSV logger starts enabled as before")
        expect("STM32_LOG  WAIT_CAN" in panel.stm32_status_label.text(),
               "STM32 sync waits for an active CAN connection")

        expect(panel.write_event(
            source="RTC",
            severity="INFO",
            event_code="RTC_WRITE_OK",
            detail="calendar updated",
        ), "GUI event is written through the panel")
        initial_file = panel.logger.last_path
        rows = read_rows(initial_file)
        expect(rows[-1]["rtc_time"] == "17/07/2026 14:16:41.46",
               "RTC timestamp provider remains attached to CSV rows")
        expect(rows[-1]["event_code"] == "RTC_WRITE_OK",
               "event code remains unchanged")

        panel.enable_checkbox.setChecked(False)
        expect(not panel.enabled, "checkbox disables event logging")
        expect("LOG  DISABLED" in panel.event_status_label.text(),
               "disabled state is rendered")
        rows = read_rows(initial_file)
        expect(rows[-1]["event_code"] == "LOG_DISABLED",
               "disable transition is logged before writes stop")

        panel.enable_checkbox.setChecked(True)
        expect(panel.enabled, "checkbox re-enables event logging")
        expect(panel.select_directory(), "selected log directory is accepted")
        expect(panel.directory == selected_directory,
               "both logging services use the selected directory")
        expect(panel.directory_input.text() == str(selected_directory),
               "selected directory is rendered in the configuration form")
        expect(panel.stm32_sync.file_path is None,
               "directory change rotates the STM32 log file")

        selected_file = panel.logger.last_path
        rows = read_rows(selected_file)
        expect(rows[-1]["event_code"] == "LOG_DIRECTORY_SELECTED",
               "directory change remains visible in the GUI event log")

        bus_holder["bus"] = object()
        panel.stm32_sync.heartbeat_rx_count = 1
        panel.stm32_sync.heartbeat_ready = True
        panel.update_stm32_status()
        expect("STM32_LOG  ACTIVE" in panel.stm32_status_label.text(),
               "connected CAN with no sync error is rendered active")

        panel.stm32_sync.last_error = "record timeout"
        panel.update_stm32_status()
        expect("STM32_LOG  WARNING" in panel.stm32_status_label.text(),
               "sync errors remain visible as warnings")
        expect("record timeout" in panel.stm32_status_label.tooltip,
               "latest sync warning remains available in the tooltip")

    print("PASS: GUI event-log panel controls, CSV writes and sync status")


if __name__ == "__main__":
    main()
