"""User-facing oscilloscope PWM controls and confirmed status."""

from PySide6.QtWidgets import (
    QCheckBox,
    QDoubleSpinBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
)

from .protocol import PWM_MAX_FREQUENCY_HZ, PWM_MIN_FREQUENCY_HZ


class PwmPanel:
    def __init__(self, apply_requested):
        self._apply_requested = apply_requested
        self.configuration_group = self._build_configuration_group()
        self.status_group = self._build_status_group()
        self.apply_button.clicked.connect(self.request_apply)

    def _build_configuration_group(self):
        self.frequency_spin = QSpinBox()
        self.frequency_spin.setRange(
            PWM_MIN_FREQUENCY_HZ, PWM_MAX_FREQUENCY_HZ
        )
        self.frequency_spin.setValue(1000)
        self.frequency_spin.setSingleStep(100)
        self.frequency_spin.setSuffix(" Hz")

        self.duty_spin = QDoubleSpinBox()
        self.duty_spin.setRange(0.0, 100.0)
        self.duty_spin.setDecimals(1)
        self.duty_spin.setSingleStep(1.0)
        self.duty_spin.setValue(50.0)
        self.duty_spin.setSuffix(" %")

        self.enabled_checkbox = QCheckBox("Output enabled")
        self.apply_button = QPushButton("Apply PWM")
        self.apply_button.setObjectName("primaryButton")

        form = QFormLayout()
        form.addRow("Frequency", self.frequency_spin)
        form.addRow("Duty cycle", self.duty_spin)

        action_layout = QHBoxLayout()
        action_layout.addWidget(self.enabled_checkbox)
        action_layout.addStretch()
        action_layout.addWidget(self.apply_button)

        group = QGroupBox("Oscilloscope PWM")
        layout = QVBoxLayout(group)
        layout.addLayout(form)
        layout.addLayout(action_layout)
        return group

    def _build_status_group(self):
        self.state_label = QLabel("Waiting for STM32")
        self.state_label.setObjectName("statusSummary")
        self.signal_label = QLabel("No confirmed output settings yet")
        self.pin_label = QLabel("Output pin: Arduino D9 (PD15 / TIM4 CH4)")
        self.pin_label.setObjectName("statusMeta")

        group = QGroupBox("PWM Output")
        layout = QVBoxLayout(group)
        layout.addWidget(self.state_label)
        layout.addWidget(self.signal_label)
        layout.addWidget(self.pin_label)
        return group

    def request_apply(self):
        self._apply_requested(
            self.enabled_checkbox.isChecked(),
            self.frequency_spin.value(),
            self.duty_spin.value(),
        )

    def render_status(self, state, frequency_hz, duty_percent, result):
        self.state_label.setText(state)

        if frequency_hz is None or duty_percent is None:
            self.signal_label.setText("No confirmed output settings yet")
            return

        self.signal_label.setText(
            f"Confirmed output: {frequency_hz:,} Hz at "
            f"{duty_percent:.1f}% duty"
        )
