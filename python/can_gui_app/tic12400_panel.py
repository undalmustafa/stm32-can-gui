"""TIC12400 device health, raw ADC, and characterization panel."""

import time
from datetime import datetime
from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QAbstractItemView,
    QFileDialog,
    QGridLayout,
    QGroupBox,
    QHeaderView,
    QHBoxLayout,
    QLabel,
    QMessageBox,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
)


class Tic12400Panel:
    """Render confirmed MCU telemetry without optimistic local state."""

    def __init__(self, clock=None):
        self._clock = clock or time.monotonic
        self._capture_requested = None
        self._clear_requested = None
        self._calibration_csv_requested = None
        self.overview_group = self._build_overview()
        self.channel_group = self._build_channel_table()
        self.calibration_group = self._build_calibration_controls()

    def set_calibration_handlers(
            self, capture_requested, clear_requested,
            calibration_csv_requested):
        self._capture_requested = capture_requested
        self._clear_requested = clear_requested
        self._calibration_csv_requested = calibration_csv_requested

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

        self.channel_table = QTableWidget(24, 9)
        self.channel_table.setHorizontalHeaderLabels((
            "Input",
            "Availability",
            "Mode",
            "Raw ADC",
            "Physical Position",
            "Generation",
            "Left Range",
            "Center Range",
            "Right Range",
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
                "--",
                "--",
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

    def _build_calibration_controls(self):
        group = QGroupBox("Three-Position Characterization")
        layout = QVBoxLayout(group)
        notice = QLabel(
            "Move all 23 fitted switches to one physical position, then start "
            "that capture. The GUI records ten complete ADC snapshots and "
            "shows the observed minimum–maximum range. Keep the switches "
            "still until capture completes. IN12 is always excluded."
        )
        notice.setWordWrap(True)
        self.calibration_status = QLabel("No position captures yet.")
        self.calibration_status.setWordWrap(True)

        self.capture_left_button = QPushButton("Capture Left")
        self.capture_center_button = QPushButton("Capture Center")
        self.capture_right_button = QPushButton("Capture Right")
        self.clear_calibration_button = QPushButton("Clear Captures")
        self.export_calibration_button = QPushButton("Export CSV…")
        self.export_calibration_button.setEnabled(False)

        self.capture_left_button.clicked.connect(
            lambda: self._start_capture("left")
        )
        self.capture_center_button.clicked.connect(
            lambda: self._start_capture("center")
        )
        self.capture_right_button.clicked.connect(
            lambda: self._start_capture("right")
        )
        self.clear_calibration_button.clicked.connect(self._clear_captures)
        self.export_calibration_button.clicked.connect(
            self._export_calibration
        )

        capture_buttons = QHBoxLayout()
        capture_buttons.addWidget(self.capture_left_button)
        capture_buttons.addWidget(self.capture_center_button)
        capture_buttons.addWidget(self.capture_right_button)

        action_buttons = QHBoxLayout()
        action_buttons.addWidget(self.clear_calibration_button)
        action_buttons.addWidget(self.export_calibration_button)
        action_buttons.addStretch()

        layout.addWidget(notice)
        layout.addWidget(self.calibration_status)
        layout.addLayout(capture_buttons)
        layout.addLayout(action_buttons)
        return group

    def _start_capture(self, position):
        if self._capture_requested is not None:
            self._capture_requested(position)

    def _clear_captures(self):
        if self._clear_requested is not None:
            self._clear_requested()

    def _export_calibration(self):
        if self._calibration_csv_requested is None:
            return

        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        default_name = f"tic12400_characterization_{timestamp}.csv"
        filename, _selected_filter = QFileDialog.getSaveFileName(
            self.calibration_group,
            "Export TIC12400 Characterization",
            default_name,
            "CSV files (*.csv);;All files (*)",
        )
        if not filename:
            return

        try:
            Path(filename).write_text(
                self._calibration_csv_requested(),
                encoding="utf-8",
            )
        except OSError as error:
            QMessageBox.critical(
                self.calibration_group,
                "Export Failed",
                str(error),
            )

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

    @staticmethod
    def _format_range(capture, channel):
        minimum = capture["minimum"][channel]
        maximum = capture["maximum"][channel]
        if minimum is None or maximum is None:
            return "--"
        if minimum == maximum:
            return str(minimum)
        return f"{minimum}–{maximum}"

    def _render_calibration(self, calibration, device_status):
        active = calibration["active_position"]
        target = calibration["sample_target"]
        positions = calibration["positions"]
        if active is not None:
            count = positions[active]["sample_count"]
            self.calibration_status.setText(
                f"Capturing {active.title()}: {count}/{target} complete "
                "snapshots. Keep every switch still."
            )
        elif calibration["last_error"] is not None:
            self.calibration_status.setText(calibration["last_error"])
        else:
            summaries = []
            for position in ("left", "center", "right"):
                count = positions[position]["sample_count"]
                summaries.append(
                    f"{position.title()} {count}/{target}"
                )
            self.calibration_status.setText(
                "Captured snapshots: " + ", ".join(summaries)
            )

        capture_buttons = (
            self.capture_left_button,
            self.capture_center_button,
            self.capture_right_button,
        )
        capture_ready = bool(
            device_status["healthy"]
            and device_status["adc_characterization"]
        )
        for button in capture_buttons:
            button.setEnabled(active is None and capture_ready)

        has_samples = any(
            capture["sample_count"] > 0
            for capture in positions.values()
        )
        self.export_calibration_button.setEnabled(has_samples)

        for channel in range(24):
            for column, position in enumerate(
                    ("left", "center", "right"), start=6):
                text = self._format_range(
                    positions[position], channel
                )
                self.channel_table.item(channel, column).setText(text)

    def render(self, device_status, channels, telemetry, calibration):
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

        self._render_calibration(calibration, device_status)
