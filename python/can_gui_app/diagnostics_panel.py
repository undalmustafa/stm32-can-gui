"""Qt presentation for live UDS product diagnostics."""

from PySide6.QtWidgets import (
    QGridLayout,
    QGroupBox,
    QLabel,
    QPushButton,
    QVBoxLayout,
)

from .protocol import (
    UDS_DID_PROTOCOL_INFO,
    UDS_DID_RESET_REASON,
    UDS_DID_RUNTIME_HEALTH,
    UDS_DID_STARTUP_HEALTH,
    decode_reset_reason_mask,
)


class DiagnosticsPanel:
    def __init__(self, refresh_requested):
        self._refresh_requested = refresh_requested
        self.summary_group = self._build_summary()
        self.protocol_group = self._build_protocol()
        self.startup_group = self._build_startup()
        self.runtime_group = self._build_runtime()
        self.reset_group = self._build_reset()

    @staticmethod
    def _value_rows(group, rows):
        layout = QGridLayout(group)
        labels = {}
        for row, name in enumerate(rows):
            value = QLabel("-")
            layout.addWidget(QLabel(name), row, 0)
            layout.addWidget(value, row, 1)
            labels[name] = value
        return layout, labels

    def _build_summary(self):
        group = QGroupBox("UDS Connection")
        layout = QVBoxLayout(group)
        self.status_label = QLabel("DISCONNECTED")
        self.detail_label = QLabel("Connect CAN to read ECU diagnostics")
        refresh = QPushButton("Refresh diagnostics")
        refresh.clicked.connect(self._refresh_requested)
        layout.addWidget(self.status_label)
        layout.addWidget(self.detail_label)
        layout.addWidget(refresh)
        return group

    def _build_protocol(self):
        group = QGroupBox("Protocol (F100)")
        _layout, self.protocol_labels = self._value_rows(group, (
            "UDS version", "Application protocol", "Log version",
            "ISO-TP capacity", "CAN IDs",
        ))
        return group

    def _build_startup(self):
        group = QGroupBox("Startup health (F101)")
        _layout, self.startup_labels = self._value_rows(group, (
            "State", "Expected mask", "Ready mask", "Failed mask",
            "First failure",
        ))
        return group

    def _build_runtime(self):
        group = QGroupBox("Runtime health (F102)")
        _layout, self.runtime_labels = self._value_rows(group, (
            "Uptime", "Latched issues", "Rejected CAN frames",
            "CAN RX lost", "CAN TX overflow", "ISO-TP errors",
        ))
        return group

    def _build_reset(self):
        group = QGroupBox("Reset reason (F103)")
        _layout, self.reset_labels = self._value_rows(group, (
            "Decoded", "Raw RCC RSR", "Capture count",
        ))
        return group

    @staticmethod
    def _set(labels, name, value):
        labels[name].setText(str(value))

    def render(self, status, detail, values):
        self.status_label.setText(status)
        self.detail_label.setText(detail)

        protocol = values.get(UDS_DID_PROTOCOL_INFO)
        if protocol:
            self._set(self.protocol_labels, "UDS version",
                      protocol["uds_version"])
            self._set(self.protocol_labels, "Application protocol",
                      protocol["protocol_version"])
            self._set(self.protocol_labels, "Log version",
                      protocol["log_version"])
            self._set(self.protocol_labels, "ISO-TP capacity",
                      f'{protocol["isotp_capacity"]} bytes')
            self._set(self.protocol_labels, "CAN IDs",
                      f'0x{protocol["request_id"]:03X} → '
                      f'0x{protocol["response_id"]:03X}')

        startup = values.get(UDS_DID_STARTUP_HEALTH)
        if startup:
            state = "DEGRADED" if startup["degraded"] else "READY"
            self._set(self.startup_labels, "State", state)
            for label, key in (("Expected mask", "expected_mask"),
                               ("Ready mask", "ready_mask"),
                               ("Failed mask", "failed_mask")):
                self._set(self.startup_labels, label,
                          f'0x{startup[key]:08X}')
            self._set(
                self.startup_labels, "First failure",
                f'resource=0x{startup["first_failed_resource"]:08X}, '
                f'result=0x{startup["first_failure_result"]:08X}',
            )

        runtime = values.get(UDS_DID_RUNTIME_HEALTH)
        if runtime:
            self._set(self.runtime_labels, "Uptime",
                      f'{runtime["uptime_ms"] / 1000.0:.1f} s')
            self._set(self.runtime_labels, "Latched issues",
                      f'0x{runtime["latched_issue_flags"]:08X}')
            self._set(self.runtime_labels, "Rejected CAN frames",
                      runtime["rejected_frames_total"])
            self._set(self.runtime_labels, "CAN RX lost",
                      runtime["can_rx_message_lost"])
            self._set(self.runtime_labels, "CAN TX overflow",
                      runtime["can_tx_queue_overflow"])
            self._set(
                self.runtime_labels, "ISO-TP errors",
                f'protocol={runtime["isotp_protocol_errors"]}, '
                f'transport={runtime["isotp_transport_failures"]}',
            )

        reset = values.get(UDS_DID_RESET_REASON)
        if reset:
            self._set(self.reset_labels, "Decoded",
                      decode_reset_reason_mask(reset["decoded_flags"]))
            self._set(self.reset_labels, "Raw RCC RSR",
                      f'0x{reset["raw_rsr"]:08X}')
            self._set(self.reset_labels, "Capture count",
                      reset["capture_count"])
