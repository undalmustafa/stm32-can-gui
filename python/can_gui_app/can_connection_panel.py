"""CAN connection controls and CAN-health status view."""

from PySide6.QtWidgets import (
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
)


class CanConnectionPanel:
    """Own connection inputs and render transport-health information."""

    def __init__(self, connect_requested):
        self._connect_requested = connect_requested
        self.configuration_group = self._build_configuration_group()
        self.health_label = self._build_health_label()

    def _build_configuration_group(self):
        self.channel_input = QLineEdit()
        self.channel_input.setText("PCAN_USBBUS1")

        self.bitrate_input = QSpinBox()
        self.bitrate_input.setRange(10000, 1000000)
        self.bitrate_input.setValue(500000)

        self.connect_button = QPushButton("Connect")
        self.connection_status_label = QLabel("Disconnected")

        group = QGroupBox("CAN Connection")
        layout = QVBoxLayout()

        channel_row = QHBoxLayout()
        channel_row.addWidget(QLabel("Channel:"))
        channel_row.addWidget(self.channel_input)

        bitrate_row = QHBoxLayout()
        bitrate_row.addWidget(QLabel("Bitrate:"))
        bitrate_row.addWidget(self.bitrate_input)

        layout.addLayout(channel_row)
        layout.addLayout(bitrate_row)
        layout.addWidget(self.connect_button)
        layout.addWidget(self.connection_status_label)
        group.setLayout(layout)

        self.connect_button.clicked.connect(self.request_connect)
        return group

    @staticmethod
    def _build_health_label():
        label = QLabel("CAN  DISCONNECTED | Adapter is not initialized")
        label.setWordWrap(True)
        label.setStyleSheet(
            "font-family: Consolas; font-weight: bold; "
            "color: #555555; padding: 4px;"
        )
        return label

    def get_connection_request(self):
        return {
            "channel": self.channel_input.text().strip(),
            "bitrate": self.bitrate_input.value(),
        }

    def request_connect(self):
        self._connect_requested(**self.get_connection_request())

    def show_connected(self, channel, bitrate):
        self.connection_status_label.setText(
            f"Connected: {channel}, {bitrate} bit/s"
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
        color = {
            "OK": "#168018",
            "WARN": "#8A6D00",
            "FAULT": "#C62828",
            "DISCONNECTED": "#555555",
        }.get(severity, "#555555")

        self.health_label.setText(
            f"CAN  {severity} | {code} | {detail} | "
            f"RX={rx_count} | EVENTS={error_event_count}"
        )
        self.health_label.setToolTip(
            f"{tooltip}\n"
            f"RX poll budget hits: {rx_budget_hit_count}\n"
            f"PCAN error frames: {error_frame_count}"
        )
        self.health_label.setStyleSheet(
            "font-family: Consolas; font-weight: bold; "
            f"color: {color}; padding: 4px;"
        )
