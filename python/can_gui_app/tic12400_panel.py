"""Read-only TIC12400 device health and raw ADC telemetry panel."""

import time

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QAbstractItemView,
    QGridLayout,
    QGroupBox,
    QHeaderView,
    QLabel,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
)


class Tic12400Panel:
    """Render confirmed MCU telemetry without optimistic local state."""

    def __init__(self, clock=None):
        self._clock = clock or time.monotonic
        self.overview_group = self._build_overview()
        self.channel_group = self._build_channel_table()
        self.calibration_group = self._build_calibration_notice()

    def _build_overview(self):
        group = QGroupBox("TIC12400-Q1 Device Overview")
        layout = QGridLayout(group)

        self.health_value = QLabel("Waiting for telemetry")
        self.device_id_value = QLabel("--")
        self.configuration_value = QLabel("Unknown")
        self.monitoring_value = QLabel("Unknown")
        self.service_result_value = QLabel("--")
        self.service_failures_value = QLabel("0")
        self.interrupt_value = QLabel("--")
        self.transaction_value = QLabel("None")
        self.snapshot_value = QLabel("Waiting for ADC telemetry")

        rows = (
            ("Health", self.health_value),
            ("Device ID", self.device_id_value),
            ("Configuration / CRC", self.configuration_value),
            ("Monitoring / ADC", self.monitoring_value),
            ("Latest Service Result", self.service_result_value),
            ("Service Failures", self.service_failures_value),
            ("Last INT Status", self.interrupt_value),
            ("Latest Transaction Flags", self.transaction_value),
            ("ADC Snapshot", self.snapshot_value),
        )
        for row, (name, value) in enumerate(rows):
            layout.addWidget(QLabel(name), row, 0)
            layout.addWidget(value, row, 1)

        layout.setColumnStretch(1, 1)
        return group

    def _build_channel_table(self):
        group = QGroupBox("24 Physical Switch Inputs")
        layout = QVBoxLayout(group)

        self.channel_table = QTableWidget(24, 6)
        self.channel_table.setHorizontalHeaderLabels((
            "Input",
            "Availability",
            "Mode",
            "Raw ADC",
            "Physical Position",
            "Generation",
        ))
        self.channel_table.setEditTriggers(
            QAbstractItemView.EditTrigger.NoEditTriggers
        )
        self.channel_table.setSelectionBehavior(
            QAbstractItemView.SelectionBehavior.SelectRows
        )
        self.channel_table.setAlternatingRowColors(True)
        self.channel_table.verticalHeader().setVisible(False)
        self.channel_table.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.Stretch
        )

        for channel in range(24):
            fitted = channel != 12
            values = (
                f"IN{channel}",
                "Fitted" if fitted else "Not fitted",
                "ADC" if fitted else "--",
                "--",
                "Uncharacterized" if fitted else "Not fitted",
                "--",
            )
            for column, value in enumerate(values):
                item = QTableWidgetItem(value)
                item.setTextAlignment(
                    Qt.AlignmentFlag.AlignCenter
                )
                self.channel_table.setItem(channel, column, item)

        layout.addWidget(self.channel_table)
        return group

    @staticmethod
    def _build_calibration_notice():
        group = QGroupBox("Three-Position Characterization")
        layout = QVBoxLayout(group)
        notice = QLabel(
            "Position thresholds are not configured yet. Move all fitted "
            "switches to left, center, and right in separate test passes and "
            "record stable raw ADC values. IN12 is unavailable because its "
            "carrier resistor is not fitted."
        )
        notice.setWordWrap(True)
        layout.addWidget(notice)
        return group

    @staticmethod
    def _yes_no(value):
        return "Yes" if value else "No"

    @staticmethod
    def _active_transaction_flags(flags):
        return [
            name.replace("_", " ").upper()
            for name, active in flags.items()
            if active
        ]

    def render(self, device_status, channels, telemetry):
        if not device_status["received"]:
            self.health_value.setText("Waiting for telemetry")
            self.health_value.setStyleSheet("color: #d19a36;")
            self.device_id_value.setText("--")
            self.configuration_value.setText("Unknown")
            self.monitoring_value.setText("Unknown")
            self.service_result_value.setText("--")
            self.service_failures_value.setText("--")
            self.interrupt_value.setText("--")
            self.transaction_value.setText("--")
        else:
            healthy = device_status["healthy"]
            if not device_status["online"]:
                health_text = "Offline"
            elif healthy:
                health_text = "Online / Healthy"
            else:
                health_text = "Online / Fault"
            self.health_value.setText(health_text)
            self.health_value.setStyleSheet(
                "color: #2da44e;" if healthy else "color: #cf222e;"
            )

            age = max(
                0.0,
                self._clock() - device_status["updated_at"],
            )
            self.health_value.setToolTip(
                f"Status received {age:.1f} seconds ago"
            )
            device_id = device_status["device_id"]
            self.device_id_value.setText(
                "--" if device_id is None else f"0x{device_id:02X}"
            )
            self.configuration_value.setText(
                "Configured: "
                f"{self._yes_no(device_status['configuration_valid'])}"
                " | CRC complete: "
                f"{self._yes_no(device_status['crc_complete'])}"
            )
            self.monitoring_value.setText(
                f"Monitoring: {self._yes_no(device_status['monitoring'])}"
                " | Raw ADC characterization: "
                f"{self._yes_no(device_status['adc_characterization'])}"
            )
            result = device_status["service_result"]
            result_code = "--" if result is None else f"0x{result:02X}"
            self.service_result_value.setText(
                f"{device_status['service_result_name']} ({result_code})"
            )
            self.service_failures_value.setText(
                str(device_status["service_failures"])
            )
            self.interrupt_value.setText(
                f"0x{device_status['last_nonzero_int_status']:04X}"
                " | POR observed: "
                f"{self._yes_no(device_status['por_observed'])}"
            )
            active_flags = self._active_transaction_flags(
                device_status["transaction_flags"]
            )
            self.transaction_value.setText(
                ", ".join(active_flags) if active_flags else "None"
            )

        generation = telemetry["generation"]
        if generation is None:
            snapshot = "Waiting for ADC telemetry"
        else:
            state = "complete" if telemetry["snapshot_complete"] else "partial"
            snapshot = (
                f"Generation {generation} | "
                f"{telemetry['received_group_count']}/8 groups | {state} | "
                f"malformed {telemetry['malformed_frames']}, "
                f"stale {telemetry['stale_frames']}, "
                f"duplicate {telemetry['duplicate_groups']}"
            )
        self.snapshot_value.setText(snapshot)

        for channel in channels:
            row = channel["channel"]
            if not channel["fitted"]:
                continue

            code = channel["adc_code"]
            generation = channel["generation"]
            raw_text = "--" if code is None else f"{code} (0x{code:03X})"
            generation_text = "--" if generation is None else str(generation)
            position = channel["state"].replace("_", " ").title()
            self.channel_table.item(row, 3).setText(raw_text)
            self.channel_table.item(row, 4).setText(position)
            self.channel_table.item(row, 5).setText(generation_text)
