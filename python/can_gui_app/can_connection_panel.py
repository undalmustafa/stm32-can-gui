"""CAN connection controls and CAN-health status view."""

import re
import sys

from PySide6.QtWidgets import (
    QComboBox,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QSpinBox,
    QWidget,
)


class CanConnectionPanel:
    """Own connection inputs and render transport-health information."""

    BACKENDS = (
        ("SocketCAN (Linux)", "socketcan", "can0"),
        ("PCAN", "pcan", "PCAN_USBBUS1"),
    )

    def __init__(self, connect_requested, platform=None):
        self._connect_requested = connect_requested
        self._platform = platform or sys.platform
        self.configuration_group = self._build_configuration_group()
        self.health_widget = self._build_health_widget()

    def _build_configuration_group(self):
        self.interface_input = QComboBox()
        for label, interface, _channel in self.BACKENDS:
            self.interface_input.addItem(label, interface)
        default_interface = (
            "socketcan" if self._platform.startswith("linux") else "pcan"
        )
        self.interface_input.setCurrentIndex(
            self.interface_input.findData(default_interface)
        )

        self.channel_input = QLineEdit()
        self.channel_input.setText(self._default_channel(default_interface))
        self.channel_input.setMinimumWidth(150)

        self.bitrate_input = QSpinBox()
        self.bitrate_input.setRange(10000, 1000000)
        self.bitrate_input.setValue(500000)
        self.bitrate_input.setSuffix(" bit/s")
        self.bitrate_input.setMinimumWidth(150)

        self.connect_button = QPushButton("Connect")
        self.connect_button.setObjectName("primaryButton")
        self.connect_button.setMinimumWidth(100)
        self.connection_status_label = QLabel("Disconnected")
        self.connection_status_label.setObjectName("connectionState")
        self.connection_status_label.setMinimumWidth(210)

        group = QGroupBox("CAN Connection")
        layout = QHBoxLayout()
        layout.addWidget(QLabel("Backend"))
        layout.addWidget(self.interface_input, 1)
        layout.addSpacing(8)
        layout.addWidget(QLabel("Channel"))
        layout.addWidget(self.channel_input, 2)
        layout.addSpacing(8)
        layout.addWidget(QLabel("Bitrate"))
        layout.addWidget(self.bitrate_input, 1)
        layout.addSpacing(8)
        layout.addWidget(self.connect_button)
        layout.addSpacing(8)
        layout.addWidget(self.connection_status_label)
        group.setLayout(layout)

        self.connect_button.clicked.connect(self.request_connect)
        self.interface_input.currentIndexChanged.connect(
            self._interface_changed
        )
        return group

    def _default_channel(self, interface):
        for _label, item_interface, channel in self.BACKENDS:
            if item_interface == interface:
                return channel
        return ""

    def _interface_changed(self, _index):
        self.channel_input.setText(
            self._default_channel(self.interface_input.currentData())
        )

    def _build_health_widget(self):
        widget = QWidget()
        widget.setObjectName("canHealthBar")

        self.health_badge = QLabel("Offline")
        self.health_badge.setObjectName("statusBadge")
        self.health_badge.setMinimumWidth(82)

        self.health_label = QLabel("Connect a CAN adapter to begin")
        self.health_label.setObjectName("healthSummary")
        self.health_label.setWordWrap(True)

        self.health_metrics_label = QLabel("No frames received")
        self.health_metrics_label.setObjectName("statusMeta")

        layout = QHBoxLayout(widget)
        layout.setContentsMargins(10, 8, 10, 8)
        layout.setSpacing(12)
        layout.addWidget(self.health_badge)
        layout.addWidget(self.health_label, 2)
        layout.addStretch()
        layout.addWidget(self.health_metrics_label)

        self.health_widget = widget
        self._style_health("DISCONNECTED")
        return widget

    def _style_health(self, severity):
        colors = {
            "OK": ("#ECFDF3", "#75C78C", "#166534"),
            "WARN": ("#FFFAEB", "#E6B94A", "#8A5A00"),
            "FAULT": ("#FEF3F2", "#E59A94", "#B42318"),
            "DISCONNECTED": ("#F2F4F7", "#B8C0CC", "#475467"),
        }
        background, border, foreground = colors.get(
            severity, colors["DISCONNECTED"]
        )
        self.health_widget.setStyleSheet(
            "QWidget#canHealthBar {"
            f"background: {background}; border: 1px solid {border}; "
            "border-radius: 5px;"
            "}"
        )
        self.health_badge.setStyleSheet(
            f"background: {foreground}; color: white; border-radius: 4px; "
            "font-weight: 700; padding: 4px 8px;"
        )
        self.health_label.setStyleSheet(
            f"color: {foreground}; font-weight: 700;"
        )

    @staticmethod
    def _health_summary(code):
        summaries = {
            "ACTIVE": "Receiving data from STM32",
            "RECOVERED_BUS_OFF": "CAN bus recovered",
            "WAIT_RX": "Waiting for STM32 data",
            "STM32_RX_STALE": "STM32 data is delayed",
            "STM32_RX_RECOVERING": "STM32 traffic is recovering",
            "STM32_RX_TIMEOUT": "STM32 stopped sending data",
            "BUS_OFF": "CAN bus is offline",
            "ERROR_PASSIVE": "CAN error rate is high",
            "BUS_HEAVY": "CAN bus has communication errors",
            "ERROR_WARNING": "CAN communication is becoming unstable",
            "DRIVER_WARNING": "CAN interface reports a warning",
            "STATUS_EXCEPTION": "Could not read CAN interface status",
            "CONNECT_FAILED": "Could not connect to the CAN adapter",
            "DISCONNECTED": "CAN adapter is not connected",
        }
        return summaries.get(code, code.replace("_", " ").title())

    def get_connection_request(self):
        return {
            "interface": self.interface_input.currentData(),
            "channel": self.channel_input.text().strip(),
            "bitrate": self.bitrate_input.value(),
        }

    def request_connect(self):
        self._connect_requested(**self.get_connection_request())

    def show_connected(self, interface, channel, bitrate):
        self.connection_status_label.setText(
            f"Connected: {interface}/{channel}, {bitrate} bit/s"
        )
        self.connection_status_label.setStyleSheet(
            "color: #168018; font-weight: bold;"
        )

    def show_disconnected(self):
        self.connection_status_label.setText("Disconnected")
        self.connection_status_label.setStyleSheet(
            "color: #C62828; font-weight: bold;"
        )

    def render_health(self, severity, code, detail, tooltip, rx_count,
                      error_event_count, rx_budget_hit_count,
                      error_frame_count):
        badge_text = {
            "OK": "Healthy",
            "WARN": "Attention",
            "FAULT": "Problem",
            "DISCONNECTED": "Offline",
        }.get(severity, "Unknown")
        age_match = re.search(r"STM32_RX_AGE=([0-9]+) ms", detail)
        if age_match is not None:
            age_text = f"Last frame {age_match.group(1)} ms ago"
        elif code == "WAIT_RX":
            age_text = "No STM32 frames yet"
        elif code == "STM32_RX_TIMEOUT":
            age_text = "Frame timeout"
        else:
            age_text = "Link status updated"

        change_word = "change" if error_event_count == 1 else "changes"
        self.health_badge.setText(badge_text)
        self.health_label.setText(self._health_summary(code))
        self.health_metrics_label.setText(
            f"{age_text}   |   {rx_count:,} frames   |   "
            f"{error_event_count} status {change_word}"
        )
        technical_tooltip = (
            f"State: {severity} / {code}\n"
            f"Detail: {detail}\n"
            f"{tooltip}\n"
            f"RX poll budget hits: {rx_budget_hit_count}\n"
            f"CAN error frames: {error_frame_count}"
        )
        self.health_widget.setToolTip(technical_tooltip)
        self.health_label.setToolTip(technical_tooltip)
        self.health_metrics_label.setToolTip(technical_tooltip)
        self._style_health(severity)
