import sys
import types
from pathlib import Path


class FakeWidget:
    def __init__(self, text=""):
        self.text = text
        self.layout = None

    def setText(self, text):
        self.text = text

    def setStyleSheet(self, _style):
        pass


class FakeLayout:
    def __init__(self, parent=None):
        self.items = []
        if parent is not None:
            parent.layout = self

    def addWidget(self, widget):
        self.items.append(widget)


class FakeHeader:
    def __init__(self):
        self.visible = True

    def setVisible(self, visible):
        self.visible = visible

    def setSectionResizeMode(self, _mode):
        pass


class FakeTable(FakeWidget):
    def __init__(self, rows, columns):
        super().__init__()
        self.rows = rows
        self.columns = columns
        self.items = {}
        self.vertical_header = FakeHeader()
        self.horizontal_header = FakeHeader()

    def setHorizontalHeaderLabels(self, labels):
        self.labels = list(labels)

    def verticalHeader(self):
        return self.vertical_header

    def horizontalHeader(self):
        return self.horizontal_header

    def setEditTriggers(self, _triggers):
        pass

    def setSelectionMode(self, _mode):
        pass

    def setItem(self, row, column, item):
        self.items[(row, column)] = item


class FakeTableItem:
    def __init__(self, text):
        self.text = text


qtwidgets = types.ModuleType("PySide6.QtWidgets")
qtwidgets.QAbstractItemView = types.SimpleNamespace(
    EditTrigger=types.SimpleNamespace(NoEditTriggers=0),
    SelectionMode=types.SimpleNamespace(NoSelection=0),
)
qtwidgets.QGroupBox = FakeWidget
qtwidgets.QHeaderView = types.SimpleNamespace(
    ResizeMode=types.SimpleNamespace(Stretch=0)
)
qtwidgets.QLabel = FakeWidget
qtwidgets.QTableWidget = FakeTable
qtwidgets.QTableWidgetItem = FakeTableItem
qtwidgets.QVBoxLayout = FakeLayout

pyside = types.ModuleType("PySide6")
pyside.QtWidgets = qtwidgets
sys.modules.setdefault("PySide6", pyside)
sys.modules.setdefault("PySide6.QtWidgets", qtwidgets)

PACKAGE_ROOT = Path(__file__).resolve().parents[1]
GUI_DIRECTORY = PACKAGE_ROOT / "upload"
if not (GUI_DIRECTORY / "can_gui_app").is_dir():
    GUI_DIRECTORY = PACKAGE_ROOT
sys.path.insert(0, str(GUI_DIRECTORY))

from can_gui_app.protocol import TIMING_SERVICE_NAMES  # noqa: E402
from can_gui_app.timing_panel import TimingPanel  # noqa: E402


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    panel = TimingPanel()
    service_timings = {
        name: {
            "enabled": True,
            "current_overrun": False,
            "overrun_latched": name == "RTC",
            "current_us": service_id + 10,
            "minimum_us": service_id + 1,
            "maximum_us": service_id + 100,
        }
        for service_id, name in TIMING_SERVICE_NAMES.items()
    }
    service_histories = {
        name: [service_id + 1, service_id + 2]
        for service_id, name in TIMING_SERVICE_NAMES.items()
    }
    panel.render(
        service_timings=service_timings,
        service_histories=service_histories,
        ack_latency={
            "p50_us": 100,
            "p95_us": 1000,
            "p99_us": 5000,
            "maximum_us": 30000,
        },
        ack_p95_history=[500, 1000],
    )

    expect(panel.table.rows == 8 and panel.table.columns == 6,
           "timing table has one row per measured firmware service")
    expect(panel.table.items[(0, 0)].text == "MAIN_LOOP"
           and panel.table.items[(0, 1)].text == "10",
           "timing table renders service name and current duration")
    rtc_row = list(TIMING_SERVICE_NAMES.values()).index("RTC")
    expect(panel.table.items[(rtc_row, 4)].text == "OVERRUN SEEN",
           "latched budget overrun is visible")
    expect("p95: 1000 us" in panel.ack_summary_label.text,
           "ACK percentile summary is visible")
    expect(panel.ack_history_label.text != "p95 history: -",
           "ACK p95 bounded-history graph is visible")

    print("PASS: timing panel table, overrun state and history graphs")


if __name__ == "__main__":
    main()
