"""CSV event-log controls and STM32 log-sync status widgets."""

from pathlib import Path

from PySide6.QtWidgets import (
    QCheckBox,
    QFileDialog,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QVBoxLayout,
)

from .csv_event_logger import CsvEventLogger
from .stm32_log_sync import Stm32LogSync


class EventLogPanel:
    """Own logging services and expose their Qt configuration/status views."""

    def __init__(self, default_directory, bus_provider, rtc_time_provider,
                 dialog_parent=None, directory_selector=None):
        self._bus_provider = bus_provider
        self._rtc_time_provider = rtc_time_provider
        self._dialog_parent = dialog_parent
        self._directory_selector = directory_selector

        self.logger = CsvEventLogger(Path(default_directory))
        self.stm32_sync = Stm32LogSync(
            bus_provider=bus_provider,
            enabled_provider=lambda: self.logger.enabled,
            directory_provider=lambda: self.logger.directory,
            status_changed=self.update_stm32_status,
        )

        self.configuration_group = self._build_configuration_group()
        self.event_status_group, self.stm32_status_group = (
            self._build_status_groups()
        )
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
        self.event_status_label = QLabel()
        self.event_status_label.setWordWrap(True)
        self.event_status_label.setStyleSheet(
            "font-family: Consolas; color: #555555;"
        )
        event_group = QGroupBox("CSV Event Log Status")
        event_layout = QVBoxLayout()
        event_layout.setContentsMargins(8, 6, 8, 6)
        event_layout.addWidget(self.event_status_label)
        event_group.setLayout(event_layout)

        self.stm32_status_label = QLabel()
        self.stm32_status_label.setWordWrap(True)
        self.stm32_status_label.setStyleSheet(
            "font-family: Consolas; color: #555555;"
        )
        stm32_group = QGroupBox("STM32 RAM Event Log Sync")
        stm32_layout = QVBoxLayout()
        stm32_layout.setContentsMargins(8, 6, 8, 6)
        stm32_layout.addWidget(self.stm32_status_label)
        stm32_group.setLayout(stm32_layout)
        return event_group, stm32_group

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
        self.update_event_status()
        return result

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
            status_text = (
                f"LOG  DISABLED | WRITES={self.logger.write_count} | "
                f"ERRORS={self.logger.error_count}"
            )
            color = "#555555"
        elif self.logger.last_error is not None:
            status_text = (
                f"LOG  FAULT | WRITES={self.logger.write_count} | "
                f"ERRORS={self.logger.error_count}"
            )
            color = "#C62828"
        else:
            active_path = self.logger.last_path or self.logger.get_file_path()
            status_text = (
                f"LOG  ENABLED | FILE={active_path.name} | "
                f"WRITES={self.logger.write_count} | "
                f"ERRORS={self.logger.error_count}"
            )
            color = "#168018"

        self.event_status_label.setText(status_text)
        tooltip = [f"Directory: {self.logger.directory}"]
        if self.logger.last_error is not None:
            tooltip.append(f"Last error: {self.logger.last_error}")
        self.event_status_label.setToolTip("\n".join(tooltip))
        self.event_status_label.setStyleSheet(
            "font-family: Consolas; font-weight: bold; "
            f"color: {color};"
        )

    def update_stm32_status(self):
        if not hasattr(self, "stm32_status_label"):
            return

        sync = self.stm32_sync
        if not self.logger.enabled:
            state = "DISABLED"
            color = "#555555"
        elif self._bus_provider() is None:
            state = "WAIT_CAN"
            color = "#555555"
        elif sync.heartbeat_rx_count == 0:
            state = "WAIT_HEARTBEAT"
            color = "#8A6D00"
        elif sync.heartbeat_timeout_active:
            state = "HEARTBEAT_TIMEOUT"
            color = "#C62828"
        elif not sync.heartbeat_ready:
            state = "LOG_NOT_READY"
            color = "#8A6D00"
        elif sync.last_error is not None:
            state = "WARNING"
            color = "#8A6D00"
        else:
            state = "ACTIVE"
            color = "#168018"

        active_file = sync.file_path.name if sync.file_path else "pending"
        status_text = (
            f"STM32_LOG  {state} | FILE={active_file} | "
            f"SAVED={sync.saved_count} | "
            f"CRC_ERR={sync.crc_error_count} | "
            f"TIMEOUT={sync.timeout_count} | "
            f"MISSED={sync.missed_count}"
        )
        if sync.last_reset_summary is not None:
            status_text += f"\nBOOT  {sync.last_reset_summary}"
        if sync.last_event_summary is not None:
            status_text += f"\nLAST  {sync.last_event_summary}"
        self.stm32_status_label.setText(status_text)
        tooltip = [f"Directory: {self.logger.directory}"]
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
        self.stm32_status_label.setToolTip("\n".join(tooltip))
        self.stm32_status_label.setStyleSheet(
            "font-family: Consolas; font-weight: bold; "
            f"color: {color};"
        )

    def process(self):
        self.stm32_sync.process()

    def reset_sync(self):
        self.stm32_sync.reset()

    def handle_message(self, message):
        self.stm32_sync.handle_message(message)
