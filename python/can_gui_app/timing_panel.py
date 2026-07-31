"""Qt presentation for firmware execution timing telemetry."""

from PySide6.QtWidgets import (
    QAbstractItemView,
    QGroupBox,
    QHeaderView,
    QLabel,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
)

from .protocol import TIMING_SERVICE_NAMES
from .timing_controller import build_sparkline


class TimingPanel:
    def __init__(self):
        self.service_names = list(TIMING_SERVICE_NAMES.values())
        self.summary_group = self._build_summary_group()
        self.history_group = self._build_history_group()

    def _build_summary_group(self):
        group = QGroupBox("Firmware Service Timing (DWT)")
        layout = QVBoxLayout(group)
        self.table = QTableWidget(len(self.service_names), 6)
        self.table.setHorizontalHeaderLabels([
            "Service",
            "Current (us)",
            "Min (us)",
            "Max (us)",
            "State",
            "Recent current",
        ])
        self.table.verticalHeader().setVisible(False)
        self.table.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.Stretch
        )
        self.table.setEditTriggers(
            QAbstractItemView.EditTrigger.NoEditTriggers
        )
        self.table.setSelectionMode(
            QAbstractItemView.SelectionMode.NoSelection
        )
        layout.addWidget(self.table)
        return group

    def _build_history_group(self):
        group = QGroupBox("Command RX-to-ACK Enqueue Latency")
        layout = QVBoxLayout(group)
        self.ack_summary_label = QLabel(
            "p50: 0 us | p95: 0 us | p99: 0 us | max: 0 us"
        )
        self.ack_history_label = QLabel("p95 history: -")
        self.ack_history_label.setStyleSheet(
            "font-family: Consolas; font-size: 15px;"
        )
        layout.addWidget(self.ack_summary_label)
        layout.addWidget(self.ack_history_label)
        return group

    @staticmethod
    def _state_text(timing):
        if not timing["enabled"]:
            return "DWT OFF"
        if timing["current_overrun"]:
            return "CURRENT OVERRUN"
        if timing["overrun_latched"]:
            return "OVERRUN SEEN"
        return "OK"

    def render(self, service_timings, service_histories,
               ack_latency, ack_p95_history):
        for row, service_name in enumerate(self.service_names):
            timing = service_timings[service_name]
            values = (
                service_name,
                str(timing["current_us"]),
                str(timing["minimum_us"]),
                str(timing["maximum_us"]),
                self._state_text(timing),
                build_sparkline(service_histories[service_name]),
            )
            for column, value in enumerate(values):
                self.table.setItem(
                    row, column, QTableWidgetItem(value)
                )

        self.ack_summary_label.setText(
            f"p50: {ack_latency['p50_us']} us | "
            f"p95: {ack_latency['p95_us']} us | "
            f"p99: {ack_latency['p99_us']} us | "
            f"max: {ack_latency['maximum_us']} us"
        )
        self.ack_history_label.setText(
            "p95 history: " + build_sparkline(ack_p95_history)
        )
