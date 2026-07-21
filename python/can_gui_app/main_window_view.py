"""Top-level tab and layout composition for the STM32 CAN GUI."""

from PySide6.QtWidgets import (
    QScrollArea,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)


class MainWindowView:
    """Assemble already-owned feature panels into the main window."""

    def __init__(self, can_connection_panel, event_log_panel,
                 can_app_panel, rtc_panel):
        self.can_connection_panel = can_connection_panel
        self.event_log_panel = event_log_panel
        self.can_app_panel = can_app_panel
        self.rtc_panel = rtc_panel

        self.tabs = QTabWidget()
        self.config_page = self._build_config_page()
        self.values_page = self._build_values_page()
        self.tabs.addTab(self.config_page, "Config")
        self.tabs.addTab(self.values_page, "Values")

        self.root_layout = QVBoxLayout()
        self.root_layout.addWidget(self.tabs)
        self.root_layout.addWidget(self.can_connection_panel.health_label)

    def _build_config_page(self):
        content = QWidget()
        layout = QVBoxLayout(content)
        layout.addWidget(self.can_connection_panel.configuration_group)
        layout.addWidget(self.event_log_panel.configuration_group)
        layout.addWidget(self.can_app_panel.slot1)
        layout.addWidget(self.can_app_panel.slot2)
        layout.addWidget(self.can_app_panel.led_control_group)
        layout.addWidget(self.rtc_panel.calendar_group)
        layout.addWidget(self.rtc_panel.alarm_configuration_group)
        layout.addStretch()

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(content)

        page = QWidget()
        page_layout = QVBoxLayout()
        page_layout.setContentsMargins(0, 0, 0, 0)
        page_layout.addWidget(scroll)
        page.setLayout(page_layout)
        return page

    def _build_values_page(self):
        page = QWidget()
        layout = QVBoxLayout()
        layout.addWidget(self.rtc_panel.values_group)
        layout.addWidget(self.event_log_panel.event_status_group)
        layout.addWidget(self.event_log_panel.stm32_status_group)
        layout.addWidget(self.can_app_panel.slot1_status_group)
        layout.addWidget(self.can_app_panel.slot2_status_group)
        layout.addWidget(self.can_app_panel.led_status_group)
        layout.addStretch()
        page.setLayout(layout)
        return page
