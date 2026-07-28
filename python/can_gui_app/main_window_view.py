"""Top-level tab and layout composition for the STM32 CAN GUI."""

from PySide6.QtWidgets import (
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QScrollArea,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)


class MainWindowView:
    """Assemble already-owned feature panels into the main window."""

    def __init__(self, can_connection_panel, event_log_panel,
                 can_app_panel, rtc_panel, pwm_panel=None,
                 tic12400_panel=None):
        self.can_connection_panel = can_connection_panel
        self.event_log_panel = event_log_panel
        self.can_app_panel = can_app_panel
        self.rtc_panel = rtc_panel
        self.pwm_panel = pwm_panel
        self.tic12400_panel = tic12400_panel

        self.header = self._build_header()
        self.tabs = QTabWidget()
        self.tabs.setDocumentMode(True)
        self.control_page = self._build_control_page()
        self.values_page = self._build_values_page()
        self.logs_page = self._build_logs_page()
        self.config_page = self.control_page
        self.tabs.addTab(self.control_page, "Control")
        self.tabs.addTab(self.values_page, "Live Data")
        if self.pwm_panel is not None:
            self.pwm_page = self._build_pwm_page()
            self.tabs.addTab(self.pwm_page, "PWM & Capture")
        if self.tic12400_panel is not None:
            self.tic12400_page = self._build_tic12400_page()
            self.tabs.addTab(self.tic12400_page, "TIC12400")
        self.tabs.addTab(self.logs_page, "Logs & Errors")

        self.root_layout = QVBoxLayout()
        self.root_layout.setContentsMargins(16, 14, 16, 12)
        self.root_layout.setSpacing(10)
        self.root_layout.addWidget(self.header)
        self.root_layout.addWidget(
            self.can_connection_panel.configuration_group
        )
        self.root_layout.addWidget(self.can_connection_panel.health_widget)
        self.root_layout.addWidget(self.tabs)

    @staticmethod
    def _build_header():
        header = QWidget()
        layout = QHBoxLayout(header)
        layout.setContentsMargins(2, 0, 2, 0)

        title = QLabel("STM32 CAN Console")
        title.setObjectName("appTitle")
        context = QLabel("STM32H7A3 | CAN / RTC")
        context.setObjectName("appContext")

        layout.addWidget(title)
        layout.addStretch()
        layout.addWidget(context)
        return header

    @staticmethod
    def _scroll_page(content):
        scroll = QScrollArea()
        scroll.setObjectName("pageScroll")
        scroll.setWidgetResizable(True)
        scroll.setWidget(content)

        page = QWidget()
        page_layout = QVBoxLayout(page)
        page_layout.setContentsMargins(0, 0, 0, 0)
        page_layout.addWidget(scroll)
        return page

    def _build_control_page(self):
        content = QWidget()
        layout = QGridLayout(content)
        layout.setContentsMargins(4, 8, 4, 8)
        layout.setHorizontalSpacing(10)
        layout.setVerticalSpacing(10)
        layout.setColumnStretch(0, 1)
        layout.setColumnStretch(1, 1)

        layout.addWidget(self.can_app_panel.slot1, 0, 0)
        layout.addWidget(self.can_app_panel.slot2, 0, 1)
        layout.addWidget(self.can_app_panel.led_control_group, 1, 0, 1, 2)
        layout.addWidget(self.rtc_panel.calendar_group, 2, 0, 1, 2)
        layout.addWidget(
            self.rtc_panel.alarm_configuration_group, 3, 0, 1, 2
        )
        layout.setRowStretch(4, 1)
        return self._scroll_page(content)

    def _build_values_page(self):
        content = QWidget()
        layout = QGridLayout(content)
        layout.setContentsMargins(4, 8, 4, 8)
        layout.setHorizontalSpacing(10)
        layout.setVerticalSpacing(10)
        layout.setColumnStretch(0, 1)
        layout.setColumnStretch(1, 1)

        layout.addWidget(self.rtc_panel.values_group, 0, 0, 1, 2)
        layout.addWidget(self.can_app_panel.slot1_status_group, 1, 0)
        layout.addWidget(self.can_app_panel.slot2_status_group, 1, 1)
        layout.addWidget(self.can_app_panel.led_status_group, 2, 0, 1, 2)
        layout.setRowStretch(3, 1)
        return self._scroll_page(content)

    def _build_logs_page(self):
        content = QWidget()
        layout = QGridLayout(content)
        layout.setContentsMargins(4, 8, 4, 8)
        layout.setHorizontalSpacing(10)
        layout.setVerticalSpacing(10)
        layout.setColumnStretch(0, 1)
        layout.setColumnStretch(1, 1)

        layout.addWidget(
            self.event_log_panel.configuration_group, 0, 0, 1, 2
        )
        layout.addWidget(self.event_log_panel.event_status_group, 1, 0)
        layout.addWidget(self.event_log_panel.stm32_status_group, 1, 1)
        layout.addWidget(self.event_log_panel.activity_group, 2, 0, 1, 2)
        layout.setRowStretch(2, 1)
        return self._scroll_page(content)

    def _build_pwm_page(self):
        content = QWidget()
        layout = QGridLayout(content)
        layout.setContentsMargins(4, 8, 4, 8)
        layout.setHorizontalSpacing(10)
        layout.setVerticalSpacing(10)
        layout.setColumnStretch(0, 1)
        layout.setColumnStretch(1, 1)
        layout.addWidget(self.pwm_panel.control_group, 0, 0, 1, 2)
        layout.addWidget(self.pwm_panel.status_group, 1, 0)
        layout.addWidget(self.pwm_panel.capture_group, 1, 1)
        layout.addWidget(self.pwm_panel.loopback_group, 2, 0, 1, 2)
        layout.setRowStretch(3, 1)
        return self._scroll_page(content)

    def _build_tic12400_page(self):
        content = QWidget()
        layout = QGridLayout(content)
        layout.setContentsMargins(4, 8, 4, 8)
        layout.setHorizontalSpacing(10)
        layout.setVerticalSpacing(10)
        layout.setColumnStretch(0, 1)
        layout.addWidget(self.tic12400_panel.channel_group, 0, 0)
        layout.setRowStretch(1, 1)
        return self._scroll_page(content)
