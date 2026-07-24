"""CSV event-log controls, status widgets and recent event view."""

from datetime import datetime
from pathlib import Path
import re

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFileDialog,
    QGroupBox,
    QHeaderView,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
)

from .csv_event_logger import CsvEventLogger
from .stm32_log_sync import Stm32LogSync


class EventLogPanel:
    """Own logging services and expose their Qt configuration/status views."""

    MAX_VISIBLE_EVENTS = 250

    def __init__(self, default_directory, bus_provider, rtc_time_provider,
                 command_sender=None, dialog_parent=None,
                 directory_selector=None):
        self._bus_provider = bus_provider
        self._rtc_time_provider = rtc_time_provider
        self._dialog_parent = dialog_parent
        self._directory_selector = directory_selector
        self._recent_events = []

        self.logger = CsvEventLogger(Path(default_directory))
        self.stm32_sync = Stm32LogSync(
            bus_provider=bus_provider,
            enabled_provider=lambda: self.logger.enabled,
            directory_provider=lambda: self.logger.directory,
            command_sender=command_sender,
            status_changed=self.update_stm32_status,
            record_observer=self._append_stm32_event,
        )

        self.configuration_group = self._build_configuration_group()
        self.event_status_group, self.stm32_status_group = (
            self._build_status_groups()
        )
        self.activity_group = self._build_activity_group()
        self.update_event_status()
        self.update_stm32_status()

    @property
    def enabled(self):
        return self.logger.enabled

    @property
    def directory(self):
        return self.logger.directory

    def _build_configuration_group(self):
        self.enable_checkbox = QCheckBox("CSV olay kaydını etkinleştir")
        self.enable_checkbox.setChecked(self.logger.enabled)

        self.directory_input = QLineEdit(str(self.logger.directory))
        self.directory_input.setReadOnly(True)
        self.select_directory_button = QPushButton("Log Klasörü Seç")

        group = QGroupBox("CSV Event Log")
        layout = QVBoxLayout()
        directory_row = QHBoxLayout()
        directory_row.addWidget(QLabel("Klasör:"))
        directory_row.addWidget(self.directory_input)
        directory_row.addWidget(self.select_directory_button)
        layout.addWidget(self.enable_checkbox)
        layout.addLayout(directory_row)
        group.setLayout(layout)

        self.enable_checkbox.toggled.connect(self.set_enabled)
        self.select_directory_button.clicked.connect(self.select_directory)
        return group

    def _build_status_groups(self):
        self.event_state_badge = QLabel("Recording")
        self.event_status_label = QLabel("Application events are being saved")
        self.event_status_label.setWordWrap(True)
        self.event_file_label = QLabel()
        self.event_file_label.setObjectName("statusMeta")
        self.event_file_label.setWordWrap(True)
        self.event_metrics_label = QLabel()
        self.event_metrics_label.setObjectName("statusMeta")

        event_heading = QHBoxLayout()
        event_heading.addWidget(self.event_state_badge)
        event_heading.addWidget(self.event_status_label, 1)

        event_group = QGroupBox("Application Event Log")
        event_layout = QVBoxLayout()
        event_layout.setContentsMargins(10, 8, 10, 10)
        event_layout.setSpacing(5)
        event_layout.addLayout(event_heading)
        event_layout.addWidget(self.event_file_label)
        event_layout.addWidget(self.event_metrics_label)
        event_group.setLayout(event_layout)

        self.stm32_state_badge = QLabel("Waiting")
        self.stm32_status_label = QLabel("Waiting for a CAN connection")
        self.stm32_status_label.setWordWrap(True)
        self.stm32_file_label = QLabel()
        self.stm32_file_label.setObjectName("statusMeta")
        self.stm32_file_label.setWordWrap(True)
        self.stm32_metrics_label = QLabel()
        self.stm32_metrics_label.setObjectName("statusMeta")
        self.stm32_latest_label = QLabel("No device events synchronized yet")
        self.stm32_latest_label.setObjectName("statusMeta")
        self.stm32_latest_label.setWordWrap(True)

        stm32_heading = QHBoxLayout()
        stm32_heading.addWidget(self.stm32_state_badge)
        stm32_heading.addWidget(self.stm32_status_label, 1)

        stm32_group = QGroupBox("STM32 Device Log")
        stm32_layout = QVBoxLayout()
        stm32_layout.setContentsMargins(10, 8, 10, 10)
        stm32_layout.setSpacing(5)
        stm32_layout.addLayout(stm32_heading)
        stm32_layout.addWidget(self.stm32_file_label)
        stm32_layout.addWidget(self.stm32_metrics_label)
        stm32_layout.addWidget(self.stm32_latest_label)
        stm32_group.setLayout(stm32_layout)
        return event_group, stm32_group

    @staticmethod
    def _set_status_badge(label, text, state):
        foreground = {
            "OK": "#166534",
            "WARN": "#8A5A00",
            "FAULT": "#B42318",
            "UNKNOWN": "#475467",
        }.get(state, "#475467")
        label.setText(text)
        label.setStyleSheet(
            f"background: {foreground}; color: white; border-radius: 4px; "
            "font-weight: 700; padding: 4px 8px;"
        )

    @staticmethod
    def _friendly_stm32_event(summary):
        if not summary:
            return "No device events synchronized yet"
        match = re.match(r"#([0-9]+) ([^|]+)(?: \| (.*))?", summary)
        if match is None:
            return summary.replace("_", " ")
        sequence, event_name, detail = match.groups()
        text = (
            f"Latest device event: {event_name.strip().replace('_', ' ').capitalize()} "
            f"(record {sequence})"
        )
        if detail:
            text += f" - {detail}"
        return text

    def _build_activity_group(self):
        self.activity_filter = QComboBox()
        self.activity_filter.addItems([
            "All events",
            "Warnings and errors",
            "Errors only",
        ])
        self.activity_filter.setMinimumWidth(170)

        self.clear_activity_button = QPushButton("Clear view")
        self.clear_activity_button.setToolTip(
            "Clear the on-screen event list without deleting CSV files"
        )

        self.activity_count_label = QLabel("0 events")
        self.activity_count_label.setObjectName("activityCount")

        self.activity_table = QTableWidget(0, 5)
        self.activity_table.setHorizontalHeaderLabels([
            "Time", "Level", "Source", "Event", "Detail"
        ])
        self.activity_table.setAlternatingRowColors(True)
        self.activity_table.setEditTriggers(
            QTableWidget.EditTrigger.NoEditTriggers
        )
        self.activity_table.setSelectionBehavior(
            QTableWidget.SelectionBehavior.SelectRows
        )
        self.activity_table.setShowGrid(False)
        self.activity_table.setHorizontalScrollBarPolicy(
            Qt.ScrollBarPolicy.ScrollBarAlwaysOff
        )
        self.activity_table.setTextElideMode(
            Qt.TextElideMode.ElideRight
        )
        self.activity_table.verticalHeader().setVisible(False)
        header = self.activity_table.horizontalHeader()
        for column in range(2):
            header.setSectionResizeMode(
                column, QHeaderView.ResizeMode.ResizeToContents
            )
        for column in (2, 3):
            header.setSectionResizeMode(
                column, QHeaderView.ResizeMode.Interactive
            )
        self.activity_table.setColumnWidth(2, 125)
        self.activity_table.setColumnWidth(3, 180)
        header.setSectionResizeMode(4, QHeaderView.ResizeMode.Stretch)
        self.activity_table.setMinimumHeight(260)

        controls = QHBoxLayout()
        controls.addWidget(QLabel("Show"))
        controls.addWidget(self.activity_filter)
        controls.addStretch()
        controls.addWidget(self.activity_count_label)
        controls.addWidget(self.clear_activity_button)

        group = QGroupBox("Recent Events")
        layout = QVBoxLayout(group)
        layout.addLayout(controls)
        layout.addWidget(self.activity_table)

        self.activity_filter.currentIndexChanged.connect(
            self._render_recent_events
        )
        self.clear_activity_button.clicked.connect(self.clear_recent_events)
        return group

    def write_event(self, source, severity, event_code, detail="",
                    direction="INTERNAL", can_id=None, payload=None):
        result = self.logger.write(
            source=source,
            severity=severity,
            event_code=event_code,
            rtc_time=self._rtc_time_provider(),
            detail=detail,
            direction=direction,
            can_id=can_id,
            payload=payload,
        )
        self._append_recent_event(
            source=source,
            severity=severity,
            event_code=event_code,
            detail=detail,
        )
        self.update_event_status()
        return result

    def _append_recent_event(self, source, severity, event_code, detail):
        self._recent_events.append({
            "time": datetime.now().astimezone().strftime("%H:%M:%S.%f")[:-3],
            "severity": str(severity or "INFO").upper(),
            "source": str(source or ""),
            "event_code": str(event_code or ""),
            "detail": str(detail or "").replace("\n", " "),
        })
        if len(self._recent_events) > self.MAX_VISIBLE_EVENTS:
            del self._recent_events[0]
        self._render_recent_events()

    def _append_stm32_event(self, record):
        self._append_recent_event(
            source=f"STM32/{record['source']}",
            severity=record["severity"],
            event_code=record["event_name"],
            detail=record["event_detail"],
        )

    def _filtered_recent_events(self):
        filter_index = self.activity_filter.currentIndex()
        if filter_index == 1:
            return [
                event for event in self._recent_events
                if event["severity"] in {"WARN", "FAULT", "ERROR"}
            ]
        if filter_index == 2:
            return [
                event for event in self._recent_events
                if event["severity"] in {"FAULT", "ERROR"}
            ]
        return list(self._recent_events)

    def _render_recent_events(self, *_args):
        events = self._filtered_recent_events()
        self.activity_table.setRowCount(len(events))
        severity_colors = {
            "INFO": QColor("#166534"),
            "WARN": QColor("#9A6700"),
            "FAULT": QColor("#B42318"),
            "ERROR": QColor("#B42318"),
        }

        for row, event in enumerate(reversed(events)):
            values = (
                event["time"],
                event["severity"],
                event["source"],
                event["event_code"],
                event["detail"],
            )
            for column, value in enumerate(values):
                item = QTableWidgetItem(value)
                item.setFlags(item.flags() & ~Qt.ItemFlag.ItemIsEditable)
                item.setToolTip(value)
                if column == 1:
                    item.setForeground(
                        severity_colors.get(value, QColor("#475467"))
                    )
                self.activity_table.setItem(row, column, item)

        shown_count = len(events)
        total_count = len(self._recent_events)
        self.activity_count_label.setText(
            f"{shown_count} of {total_count} events"
        )

    def clear_recent_events(self):
        self._recent_events.clear()
        self._render_recent_events()

    def set_enabled(self, enabled):
        if enabled:
            self.logger.enabled = True
            self.write_event(
                source="APPLICATION",
                severity="INFO",
                event_code="LOG_ENABLED",
                detail="CSV event logging enabled",
            )
        else:
            self.write_event(
                source="APPLICATION",
                severity="INFO",
                event_code="LOG_DISABLED",
                detail="CSV event logging disabled",
            )
            self.logger.enabled = False
            self.update_event_status()

        self.update_stm32_status()

    def select_directory(self):
        if self._directory_selector is None:
            selected_directory = QFileDialog.getExistingDirectory(
                self._dialog_parent,
                "CSV Log Klasörünü Seç",
                str(self.logger.directory),
            )
        else:
            selected_directory = self._directory_selector(
                str(self.logger.directory)
            )

        if not selected_directory:
            return False

        self.logger.set_directory(selected_directory)
        self.stm32_sync.file_path = None
        self.directory_input.setText(selected_directory)
        self.write_event(
            source="APPLICATION",
            severity="INFO",
            event_code="LOG_DIRECTORY_SELECTED",
            detail=f"Log directory changed to {selected_directory}",
        )
        self.update_event_status()
        self.update_stm32_status()
        return True

    def update_event_status(self):
        if not hasattr(self, "event_status_label"):
            return

        if not self.logger.enabled:
            badge_text = "Paused"
            state = "UNKNOWN"
            status_text = "Application event recording is off"
            file_text = f"Folder: {self.logger.directory}"
        elif self.logger.last_error is not None:
            badge_text = "Problem"
            state = "FAULT"
            status_text = "Could not write the application event log"
            file_text = f"Folder: {self.logger.directory}"
        else:
            active_path = self.logger.last_path or self.logger.get_file_path()
            badge_text = "Recording"
            state = "OK"
            status_text = "Application events are being saved"
            file_text = f"Saving to {active_path.name}"

        self._set_status_badge(self.event_state_badge, badge_text, state)
        self.event_status_label.setText(status_text)
        self.event_file_label.setText(file_text)
        error_word = "error" if self.logger.error_count == 1 else "errors"
        self.event_metrics_label.setText(
            f"{self.logger.write_count:,} events saved   |   "
            f"{self.logger.error_count} write {error_word}"
        )
        tooltip = [f"Directory: {self.logger.directory}"]
        if self.logger.last_error is not None:
            tooltip.append(f"Last error: {self.logger.last_error}")
        tooltip_text = "\n".join(tooltip)
        for label in (
            self.event_state_badge,
            self.event_status_label,
            self.event_file_label,
            self.event_metrics_label,
        ):
            label.setToolTip(tooltip_text)

    def update_stm32_status(self):
        if not hasattr(self, "stm32_status_label"):
            return

        sync = self.stm32_sync
        if not self.logger.enabled:
            state = "DISABLED"
        elif self._bus_provider() is None:
            state = "WAIT_CAN"
        elif sync.heartbeat_rx_count == 0:
            state = "WAIT_HEARTBEAT"
        elif sync.heartbeat_timeout_active:
            state = "HEARTBEAT_TIMEOUT"
        elif not sync.heartbeat_ready:
            state = "LOG_NOT_READY"
        elif sync.last_error is not None:
            state = "WARNING"
        else:
            state = "ACTIVE"

        presentations = {
            "DISABLED": (
                "Paused", "UNKNOWN", "Device log synchronization is off"
            ),
            "WAIT_CAN": (
                "Waiting", "UNKNOWN", "Connect CAN to synchronize device logs"
            ),
            "WAIT_HEARTBEAT": (
                "Waiting", "WARN", "Waiting for the STM32 log service"
            ),
            "HEARTBEAT_TIMEOUT": (
                "Problem", "FAULT", "STM32 log service stopped responding"
            ),
            "LOG_NOT_READY": (
                "Attention", "WARN", "STM32 log storage is not ready"
            ),
            "WARNING": (
                "Attention", "WARN", "Device log synchronization needs attention"
            ),
            "ACTIVE": (
                "Up to date", "OK", "STM32 event log is synchronized"
            ),
        }
        badge_text, visual_state, status_text = presentations[state]
        self._set_status_badge(
            self.stm32_state_badge, badge_text, visual_state
        )
        self.stm32_status_label.setText(status_text)
        if sync.file_path is None:
            self.stm32_file_label.setText(
                "A file will be created after the first device record"
            )
        else:
            self.stm32_file_label.setText(f"Saving to {sync.file_path.name}")
        self.stm32_metrics_label.setText(
            f"{sync.saved_count:,} records saved   |   "
            f"{sync.crc_error_count} CRC errors   |   "
            f"{sync.timeout_count} timeouts   |   "
            f"{sync.missed_count} missed"
        )
        latest_text = self._friendly_stm32_event(sync.last_event_summary)
        if sync.last_reset_summary is not None:
            latest_text += f"\nPrevious restart: {sync.last_reset_summary}"
        self.stm32_latest_label.setText(latest_text)
        tooltip = [f"Directory: {self.logger.directory}"]
        tooltip.append(f"Internal state: {state}")
        if sync.last_error is not None:
            tooltip.append(f"Last warning: {sync.last_error}")
        if sync.heartbeat_timeout_active:
            tooltip.append("STM32 log heartbeat timeout (>350 ms)")
        tooltip.append(
            f"Heartbeat timeout count: {sync.heartbeat_timeout_count}"
        )
        tooltip.append(
            "Heartbeat overwrite flag: "
            f"{'SET' if sync.heartbeat_overwrite_detected else 'CLEAR'}"
        )
        tooltip.append(f"Protocol errors: {sync.protocol_error_count}")
        tooltip.append(f"File write errors: {sync.write_error_count}")
        tooltip_text = "\n".join(tooltip)
        for label in (
            self.stm32_state_badge,
            self.stm32_status_label,
            self.stm32_file_label,
            self.stm32_metrics_label,
            self.stm32_latest_label,
        ):
            label.setToolTip(tooltip_text)

    def process(self):
        self.stm32_sync.process()

    def reset_sync(self):
        self.stm32_sync.reset()

    def handle_message(self, message):
        self.stm32_sync.handle_message(message)
