"""PWM controls, input-capture telemetry, and loopback testing."""

import math

from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import (
    QGridLayout, QGroupBox, QHBoxLayout, QLabel, QProgressBar,
    QPushButton, QSlider, QSpinBox, QVBoxLayout,
)


class PwmPanel:
    SWEEP_FREQUENCIES = (1_000, 10_000, 100_000, 500_000)

    def __init__(self, command_requested):
        self._command_requested = command_requested
        self._syncing = False
        self._sweep_index = -1
        self._sweep_results = []
        self._sweep_running = False
        self._mismatch_observations = 0
        self._latest_pwm_status = {
            "running": False, "frequency_hz": 0, "duty_percent": 0
        }
        self._latest_capture_status = {
            "signal_detected": False,
            "frequency_hz": 0,
            "duty_percent": 0,
            "edge_count": 0,
        }
        self.control_group = self._build_controls()
        self.status_group = self._build_status()
        self.capture_group = self._build_capture()
        self.loopback_group = self._build_loopback()
        self.sweep_timer = QTimer()
        # Status and capture telemetry are each published every 500 ms.
        # Three telemetry periods prevent comparing a new target with the
        # previous target's capture sample.
        self.sweep_timer.setInterval(1500)
        self.sweep_timer.timeout.connect(self._advance_sweep)

    def _build_controls(self):
        group = QGroupBox("PWM Output Control")
        layout = QGridLayout(group)
        self.frequency_slider = QSlider(Qt.Orientation.Horizontal)
        self.frequency_slider.setRange(0, 6000)
        self.frequency_spin = QSpinBox()
        self.frequency_spin.setRange(1, 1_000_000)
        self.frequency_spin.setSuffix(" Hz")
        self.frequency_spin.setValue(10_000)
        self.frequency_slider.setValue(self._frequency_to_slider(10_000))
        self.frequency_slider.valueChanged.connect(self._slider_changed)
        self.frequency_spin.valueChanged.connect(self._spin_changed)
        self.duty_slider = QSlider(Qt.Orientation.Horizontal)
        self.duty_slider.setRange(0, 100)
        self.duty_slider.setValue(90)
        self.duty_spin = QSpinBox()
        self.duty_spin.setRange(0, 100)
        self.duty_spin.setSuffix(" %")
        self.duty_spin.setValue(90)
        self.duty_slider.valueChanged.connect(self.duty_spin.setValue)
        self.duty_spin.valueChanged.connect(self.duty_slider.setValue)
        layout.addWidget(QLabel("Frequency"), 0, 0)
        layout.addWidget(self.frequency_slider, 0, 1)
        layout.addWidget(self.frequency_spin, 0, 2)
        layout.addWidget(QLabel("Duty Cycle"), 1, 0)
        layout.addWidget(self.duty_slider, 1, 1)
        layout.addWidget(self.duty_spin, 1, 2)

        presets = QHBoxLayout()
        for frequency, label in (
            (1_000, "1 kHz"), (10_000, "10 kHz"),
            (100_000, "100 kHz"), (1_000_000, "1 MHz")
        ):
            button = QPushButton(label)
            button.clicked.connect(
                lambda _checked=False, value=frequency:
                self.frequency_spin.setValue(value)
            )
            presets.addWidget(button)
        layout.addLayout(presets, 2, 1, 1, 2)

        enable = QPushButton("Enable PWM")
        enable.setObjectName("primaryButton")
        disable = QPushButton("Disable PWM")
        enable.clicked.connect(lambda: self._send(True))
        disable.clicked.connect(lambda: self._send(False))
        buttons = QHBoxLayout()
        buttons.addWidget(enable)
        buttons.addWidget(disable)
        layout.addLayout(buttons, 3, 1, 1, 2)
        return group

    def _build_status(self):
        group = QGroupBox("PWM Status")
        layout = QGridLayout(group)
        self.pwm_state = QLabel("Stopped")
        self.pwm_frequency = QLabel("0 Hz")
        self.pwm_duty = QLabel("0%")
        for row, (name, value) in enumerate((
            ("State", self.pwm_state),
            ("Actual Frequency", self.pwm_frequency),
            ("Actual Duty", self.pwm_duty),
            ("Output Pin", QLabel("PA0 (TIM2 CH1)")),
        )):
            layout.addWidget(QLabel(name), row, 0)
            layout.addWidget(value, row, 1)
        return group

    def _build_capture(self):
        group = QGroupBox("Input Capture")
        layout = QGridLayout(group)
        self.capture_state = QLabel("No signal")
        self.capture_frequency = QLabel("0 Hz")
        self.capture_duty = QLabel("0%")
        self.capture_edges = QLabel("0")
        values = (
            ("Signal Status", self.capture_state),
            ("Frequency", self.capture_frequency),
            ("Duty Cycle", self.capture_duty),
            ("Estimated Edges (low 16 bits)", self.capture_edges),
            ("Input Pin", QLabel("PA6 (TIM3 CH1)")),
            ("Range", QLabel("16 Hz – 500 kHz")),
        )
        for row, (name, value) in enumerate(values):
            layout.addWidget(QLabel(name), row, 0)
            layout.addWidget(value, row, 1)
        self.duty_bar = QProgressBar()
        self.duty_bar.setRange(0, 100)
        self.duty_bar.setFormat("%p% high")
        layout.addWidget(self.duty_bar, len(values), 0, 1, 2)
        return group

    def _build_loopback(self):
        group = QGroupBox("Loopback Self-Test")
        layout = QVBoxLayout(group)
        self.loopback_result = QLabel(
            "Connect D32 (PA0) to D12 (PA6)."
        )
        self.sweep_result = QLabel("No sweep run yet.")
        self.sweep_button = QPushButton("Run Frequency Sweep")
        self.sweep_button.clicked.connect(self.start_sweep)
        layout.addWidget(self.loopback_result)
        layout.addWidget(self.sweep_result)
        layout.addWidget(self.sweep_button)
        return group

    @staticmethod
    def _frequency_to_slider(value):
        return round(math.log10(max(1, value)) * 1000)

    @staticmethod
    def _slider_to_frequency(value):
        return max(1, min(1_000_000, round(10 ** (value / 1000))))

    def _slider_changed(self, value):
        if not self._syncing:
            self._syncing = True
            self.frequency_spin.setValue(self._slider_to_frequency(value))
            self._syncing = False

    def _spin_changed(self, value):
        if not self._syncing:
            self._syncing = True
            self.frequency_slider.setValue(self._frequency_to_slider(value))
            self._syncing = False

    def _send(self, enabled):
        self._command_requested(
            self.frequency_spin.value(), self.duty_spin.value(), enabled
        )

    def start_sweep(self):
        self._sweep_index = -1
        self._sweep_results = []
        self._sweep_running = True
        self._pre_sweep_frequency = self.frequency_spin.value()
        self._pre_sweep_duty = self.duty_spin.value()
        self._pre_sweep_running = self._latest_pwm_status["running"]
        # Fifty percent is representable across the complete capture range.
        # High requested duties quantize to 100% at 500 kHz, eliminating the
        # falling edge required by PWM-input duty measurement.
        self.duty_spin.setValue(50)
        self.sweep_button.setEnabled(False)
        self.sweep_result.setText("Sweep starting at 1 kHz / 50%…")
        self._advance_sweep()
        self.sweep_timer.start()

    def _advance_sweep(self):
        if self._sweep_index >= 0:
            self._record_sweep_result(
                self.SWEEP_FREQUENCIES[self._sweep_index]
            )

        self._sweep_index += 1
        if self._sweep_index >= len(self.SWEEP_FREQUENCIES):
            self.sweep_timer.stop()
            self._sweep_running = False
            self.sweep_button.setEnabled(True)
            passed = sum(result["passed"] for result in self._sweep_results)
            total = len(self.SWEEP_FREQUENCIES)
            failed = [
                result for result in self._sweep_results
                if not result["passed"]
            ]
            detail = ""
            if failed:
                first = failed[0]
                detail = (
                    f"; first failure {first['expected']:,} Hz → "
                    f"{first['measured']:,} Hz/{first['duty']}%"
                )
            self.sweep_result.setText(
                f"{'PASS' if passed == total else 'FAIL'}: "
                f"{passed}/{total} frequencies within tolerance{detail}"
            )
            self.frequency_spin.setValue(self._pre_sweep_frequency)
            self.duty_spin.setValue(self._pre_sweep_duty)
            self._send(self._pre_sweep_running)
            return
        target = self.SWEEP_FREQUENCIES[self._sweep_index]
        self.frequency_spin.setValue(target)
        self.sweep_result.setText(
            f"Sweep {self._sweep_index + 1}/{len(self.SWEEP_FREQUENCIES)}: "
            f"waiting for {target:,} Hz / 50% telemetry…"
        )
        self._send(True)

    @staticmethod
    def _within_frequency_tolerance(measured, expected):
        return abs(measured - expected) <= max(2, expected * 0.02)

    def _record_sweep_result(self, expected):
        pwm = self._latest_pwm_status
        capture = self._latest_capture_status
        passed = (
            pwm["running"]
            and capture["signal_detected"]
            and self._within_frequency_tolerance(
                pwm["frequency_hz"], expected
            )
            and self._within_frequency_tolerance(
                capture["frequency_hz"], expected
            )
            and abs(capture["duty_percent"] - pwm["duty_percent"]) <= 2
        )
        self._sweep_results.append({
            "expected": expected,
            "measured": capture["frequency_hz"],
            "duty": capture["duty_percent"],
            "passed": passed,
        })

    def render_status(self, pwm_status, input_capture_status):
        self._latest_pwm_status = dict(pwm_status)
        self._latest_capture_status = dict(input_capture_status)
        running = pwm_status["running"]
        measured = input_capture_status["frequency_hz"]
        detected = input_capture_status["signal_detected"]
        self.pwm_state.setText("Running" if running else "Stopped")
        self.pwm_frequency.setText(f'{pwm_status["frequency_hz"]:,} Hz')
        self.pwm_duty.setText(f'{pwm_status["duty_percent"]}%')
        self.capture_state.setText("Detected" if detected else "No signal")
        self.capture_frequency.setText(f"{measured:,} Hz")
        self.capture_duty.setText(
            f'{input_capture_status["duty_percent"]}%'
        )
        self.capture_edges.setText(str(input_capture_status["edge_count"]))
        self.duty_bar.setValue(
            input_capture_status["duty_percent"] if detected else 0
        )

        if running and detected:
            expected = pwm_status["frequency_hz"]
            matched = (
                self._within_frequency_tolerance(measured, expected)
                and abs(input_capture_status["duty_percent"]
                        - pwm_status["duty_percent"]) <= 2
            )
            if matched:
                self._mismatch_observations = 0
                state = "matched"
            else:
                self._mismatch_observations += 1
                state = (
                    "settling"
                    if self._mismatch_observations < 3
                    else "mismatch"
                )
            self.loopback_result.setText(
                f"Loopback {state}: output {expected:,} Hz/"
                f"{pwm_status['duty_percent']}%, capture {measured:,} Hz/"
                f"{input_capture_status['duty_percent']}%"
            )
        elif running:
            self._mismatch_observations = 0
            self.loopback_result.setText("PWM running; waiting for capture…")
        else:
            self._mismatch_observations = 0
            self.loopback_result.setText("PWM stopped.")
