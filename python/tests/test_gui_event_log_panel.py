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

    def setObjectName(self, name):
        self.object_name = name

    def setMinimumWidth(self, width):
        self.minimum_width = width

    def setMinimumHeight(self, height):
        self.minimum_height = height


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


class FakeComboBox(FakeWidget):
    def __init__(self):
        super().__init__()
        self.items = []
        self.index = 0
        self.currentIndexChanged = FakeSignal()

    def addItems(self, items):
        self.items.extend(items)

    def currentIndex(self):
        return self.index

    def setCurrentIndex(self, index):
        self.index = index
        self.currentIndexChanged.emit(index)


class FakeHeader:
    def __init__(self):
        self.visible = True

    def setVisible(self, visible):
        self.visible = visible

    def setSectionResizeMode(self, *_args):
        pass


class FakeTableWidget(FakeWidget):
    class EditTrigger:
        NoEditTriggers = 0

    class SelectionBehavior:
        SelectRows = 1

    def __init__(self, rows, columns):
        super().__init__()
        self.row_count = rows
        self.column_count = columns
        self.table_items = {}
        self._horizontal_header = FakeHeader()
        self._vertical_header = FakeHeader()

    def setHorizontalHeaderLabels(self, labels):
        self.headers = labels

    def setAlternatingRowColors(self, _enabled):
        pass

    def setEditTriggers(self, _triggers):
        pass

    def setSelectionBehavior(self, _behavior):
        pass

    def setShowGrid(self, _show):
        pass

    def setHorizontalScrollBarPolicy(self, _policy):
        pass

    def setTextElideMode(self, _mode):
        pass

    def verticalHeader(self):
        return self._vertical_header

    def horizontalHeader(self):
        return self._horizontal_header

    def setRowCount(self, count):
        self.row_count = count
        self.table_items = {
            position: item for position, item in self.table_items.items()
            if position[0] < count
        }

    def setItem(self, row, column, item):
        self.table_items[(row, column)] = item

    def setColumnWidth(self, column, width):
        self.column_widths = getattr(self, "column_widths", {})
        self.column_widths[column] = width


class FakeTableItem:
    def __init__(self, text):
        self.text = text
        self.item_flags = 1
        self.foreground = None

    def flags(self):
        return self.item_flags

    def setFlags(self, flags):
        self.item_flags = flags

    def setForeground(self, color):
        self.foreground = color

    def setToolTip(self, tooltip):
        self.tooltip = tooltip


class FakeColor:
    def __init__(self, value):
        self.value = value


class FakeLayout:
    def __init__(self, *_args):
        self.items = []

    def addWidget(self, widget, *_args):
        self.items.append(widget)

    def addLayout(self, layout):
        self.items.append(layout)

    def addStretch(self):
        self.items.append("stretch")

    def setContentsMargins(self, *_args):
        pass

    def setSpacing(self, _spacing):
        pass


class FakeFileDialog:
    @staticmethod
    def getExistingDirectory(*_args):
        return ""


qtwidgets = types.ModuleType("PySide6.QtWidgets")
qtwidgets.QCheckBox = FakeCheckBox
qtwidgets.QComboBox = FakeComboBox
qtwidgets.QFileDialog = FakeFileDialog
qtwidgets.QGroupBox = FakeWidget
qtwidgets.QHeaderView = type(
    "FakeHeaderView",
    (), {"ResizeMode": type(
        "ResizeMode", (), {
            "ResizeToContents": 0,
            "Stretch": 1,
            "Interactive": 2,
        }
    )},
)
qtwidgets.QHBoxLayout = FakeLayout
qtwidgets.QLabel = FakeWidget
qtwidgets.QLineEdit = FakeWidget
qtwidgets.QPushButton = FakeButton
qtwidgets.QTableWidget = FakeTableWidget
qtwidgets.QTableWidgetItem = FakeTableItem
qtwidgets.QVBoxLayout = FakeLayout

qtcore = types.ModuleType("PySide6.QtCore")
qtcore.Qt = types.SimpleNamespace(
    ItemFlag=types.SimpleNamespace(ItemIsEditable=1),
    ScrollBarPolicy=types.SimpleNamespace(ScrollBarAlwaysOff=1),
    TextElideMode=types.SimpleNamespace(ElideRight=1),
)
qtgui = types.ModuleType("PySide6.QtGui")
qtgui.QColor = FakeColor

pyside = types.ModuleType("PySide6")
sys.modules.setdefault("PySide6.QtCore", qtcore)
sys.modules.setdefault("PySide6.QtGui", qtgui)
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

        expect(
            panel.event_status_label.text()
            == "Application events are being saved",
            "CSV logger starts with a plain-language recording state",
        )
        expect(
            panel.stm32_status_label.text()
            == "Connect CAN to synchronize device logs",
            "STM32 sync explains what it is waiting for",
        )

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
        expect(panel.activity_table.row_count == 1,
               "recent event view receives successful writes")
        expect(
            panel.activity_table.table_items[(0, 3)].text == "RTC_WRITE_OK",
            "recent event view exposes the newest event code",
        )

        panel.write_event(
            source="CAN",
            severity="FAULT",
            event_code="BUS_OFF",
            detail="controller entered bus-off",
        )
        panel.activity_filter.setCurrentIndex(2)
        expect(panel.activity_table.row_count == 1,
               "error filter hides informational events")
        expect(panel.activity_table.table_items[(0, 3)].text == "BUS_OFF",
               "error filter retains fault events")
        panel.activity_filter.setCurrentIndex(0)

        panel.enable_checkbox.setChecked(False)
        expect(not panel.enabled, "checkbox disables event logging")
        expect(panel.event_status_label.text()
               == "Application event recording is off",
               "disabled state is explained")
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
        expect(panel.stm32_status_label.text()
               == "STM32 event log is synchronized",
               "connected CAN with no sync error is rendered active")

        panel.stm32_sync.last_error = "record timeout"
        panel.update_stm32_status()
        expect(panel.stm32_status_label.text()
               == "Device log synchronization needs attention",
               "sync errors are explained as warnings")
        expect("record timeout" in panel.stm32_status_label.tooltip,
               "latest sync warning remains available in the tooltip")

        panel.clear_activity_button.clicked.emit()
        expect(panel.activity_table.row_count == 0,
               "clear view removes only the visible recent-event history")

    print("PASS: GUI event-log panel controls, CSV writes and sync status")


if __name__ == "__main__":
    main()
