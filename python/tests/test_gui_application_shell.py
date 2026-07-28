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

    def emit(self):
        for callback in self.callbacks:
            callback()


class FakeWidget:
    def __init__(self, *_args, **_kwargs):
        self.layout = None

    def setLayout(self, layout):
        self.layout = layout

    def setObjectName(self, name):
        self.object_name = name


class FakeLayout:
    def __init__(self, parent=None):
        self.items = []
        self.margins = None
        if parent is not None:
            parent.setLayout(self)

    def addWidget(self, widget, *_args):
        self.items.append(widget)

    def addStretch(self):
        self.items.append("stretch")

    def setContentsMargins(self, *margins):
        self.margins = margins

    def setSpacing(self, _spacing):
        pass

    def setHorizontalSpacing(self, _spacing):
        pass

    def setVerticalSpacing(self, _spacing):
        pass

    def setColumnStretch(self, *_args):
        pass

    def setRowStretch(self, *_args):
        pass


class FakeScrollArea(FakeWidget):
    def __init__(self):
        super().__init__()
        self.resizable = False
        self.widget = None

    def setWidgetResizable(self, resizable):
        self.resizable = resizable

    def setWidget(self, widget):
        self.widget = widget


class FakeTabWidget(FakeWidget):
    def __init__(self):
        super().__init__()
        self.tabs = []

    def addTab(self, page, title):
        self.tabs.append((page, title))

    def setDocumentMode(self, _enabled):
        pass


class FakeTimer:
    instances = []

    def __init__(self):
        self.timeout = FakeSignal()
        self.period_ms = None
        self.timer_type = None
        FakeTimer.instances.append(self)

    def setTimerType(self, timer_type):
        self.timer_type = timer_type

    def start(self, period_ms):
        self.period_ms = period_ms


qtcore = types.ModuleType("PySide6.QtCore")
qtcore.Qt = types.SimpleNamespace(
    TimerType=types.SimpleNamespace(PreciseTimer="precise")
)
qtcore.QTimer = FakeTimer
qtwidgets = types.ModuleType("PySide6.QtWidgets")
qtwidgets.QGridLayout = FakeLayout
qtwidgets.QHBoxLayout = FakeLayout
qtwidgets.QLabel = FakeWidget
qtwidgets.QScrollArea = FakeScrollArea
qtwidgets.QTabWidget = FakeTabWidget
qtwidgets.QVBoxLayout = FakeLayout
qtwidgets.QWidget = FakeWidget

pyside = types.ModuleType("PySide6")
pyside.QtCore = qtcore
pyside.QtWidgets = qtwidgets
sys.modules.setdefault("PySide6", pyside)
sys.modules.setdefault("PySide6.QtCore", qtcore)
sys.modules.setdefault("PySide6.QtWidgets", qtwidgets)

from can_gui_app.application_timers import ApplicationTimers  # noqa: E402
from can_gui_app.main_window_view import MainWindowView  # noqa: E402


class Panel:
    pass


def widget(name):
    item = FakeWidget()
    item.name = name
    return item


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    connection = Panel()
    connection.configuration_group = widget("connection_config")
    connection.health_label = widget("can_health")
    connection.health_widget = widget("can_health_bar")

    event_log = Panel()
    event_log.configuration_group = widget("log_config")
    event_log.event_status_group = widget("event_status")
    event_log.stm32_status_group = widget("stm32_status")
    event_log.activity_group = widget("recent_events")

    can_app = Panel()
    can_app.slot1 = widget("slot1_config")
    can_app.slot2 = widget("slot2_config")
    can_app.led_control_group = widget("led_config")
    can_app.slot1_status_group = widget("slot1_status")
    can_app.slot2_status_group = widget("slot2_status")
    can_app.led_status_group = widget("led_status")

    rtc = Panel()
    rtc.calendar_group = widget("rtc_calendar")
    rtc.alarm_configuration_group = widget("rtc_alarm_config")
    rtc.values_group = widget("rtc_values")

    view = MainWindowView(connection, event_log, can_app, rtc)
    expect([title for _page, title in view.tabs.tabs] == [
        "Control", "Live Data", "Logs & Errors"
    ], "task-oriented tab order remains stable")
    expect(view.root_layout.items == [
        view.header,
        connection.configuration_group,
        connection.health_widget,
        view.tabs,
    ], "connection and CAN health remain visible above every page")

    config_scroll = view.config_page.layout.items[0]
    config_names = [
        getattr(item, "name", item)
        for item in config_scroll.widget.layout.items
    ]
    expect(config_names == [
        "slot1_config",
        "slot2_config",
        "led_config",
        "rtc_calendar",
        "rtc_alarm_config",
    ], "Control page contains only operational controls")
    expect(config_scroll.resizable,
           "Config page remains vertically scrollable")

    values_scroll = view.values_page.layout.items[0]
    value_names = [
        getattr(item, "name", item)
        for item in values_scroll.widget.layout.items
    ]
    expect(value_names == [
        "rtc_values",
        "slot1_status",
        "slot2_status",
        "led_status",
    ], "Live Data page contains current device state")

    logs_scroll = view.logs_page.layout.items[0]
    log_names = [
        getattr(item, "name", item)
        for item in logs_scroll.widget.layout.items
    ]
    expect(log_names == [
        "log_config",
        "event_status",
        "stm32_status",
        "recent_events",
    ], "Logs & Errors owns logging controls, status and recent events")

    tic12400 = Panel()
    tic12400.channel_group = widget("tic12400_channels")
    view_with_tic12400 = MainWindowView(
        connection,
        event_log,
        can_app,
        rtc,
        tic12400_panel=tic12400,
    )
    expect(
        [title for _page, title in view_with_tic12400.tabs.tabs] == [
            "Control", "Live Data", "TIC12400", "Logs & Errors"
        ],
        "TIC12400 telemetry is composed as an optional dedicated page",
    )
    tic12400_scroll = view_with_tic12400.tic12400_page.layout.items[0]
    tic12400_names = [
        getattr(item, "name", item)
        for item in tic12400_scroll.widget.layout.items
    ]
    expect(tic12400_names == [
        "tic12400_channels",
    ], "TIC12400 page contains only end-user switch states")

    timer_calls = []
    timers = ApplicationTimers(
        can_rx_poll=lambda: timer_calls.append("rx"),
        can_health_poll=lambda: timer_calls.append("health"),
        stm32_log_sync=lambda: timer_calls.append("log"),
    )
    expect(timers.can_rx_timer.period_ms == 50,
           "CAN RX polling remains 50 ms")
    expect(timers.stm32_log_sync_timer.period_ms == 50,
           "STM32 log sync remains 50 ms")
    expect(timers.can_health_timer.period_ms == 250,
           "CAN health polling remains 250 ms")

    timers.can_rx_timer.timeout.emit()
    timers.can_health_timer.timeout.emit()
    timers.stm32_log_sync_timer.timeout.emit()
    expect(timer_calls == ["rx", "health", "log"],
           "each timer retains its original callback")

    print("PASS: GUI main-window composition and timer periods")


if __name__ == "__main__":
    main()
