"""PWM controls, input-capture telemetry, and loopback testing."""

import math

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QGridLayout, QGroupBox, QHBoxLayout, QLabel, QProgressBar,
    QPushButton, QSlider, QSpinBox, QVBoxLayout,
)


class PwmPanel:
    SELF_TEST_STATE_NAMES = {
        0: "Idle",
        1: "Running",
        2: "Passed",
        3: "Failed",
        4: "Cancelled",
        5: "Firmware error",
    }

    def __init__(self, command_requested, self_test_requested=None):
        self._command_requested = command_requested
        self._self_test_requested = self_test_requested or (
            lambda _start: False
        )
        self._syncing = False
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

        self.enable_button = QPushButton("Enable PWM")
        self.enable_button.setObjectName("primaryButton")
        self.disable_button = QPushButton("Disable PWM")
        self.enable_button.clicked.connect(lambda: self._send(True))
        self.disable_button.clicked.connect(lambda: self._send(False))
        buttons = QHBoxLayout()
        buttons.addWidget(self.enable_button)
        buttons.addWidget(self.disable_button)
        layout.addLayout(buttons, 3, 1, 1, 2)
        return group

    def _build_status(self):
        group = QGroupBox("PWM Status")
        layout = QGridLayout(group)
        self.pwm_state = QLabel("Stopped")
        self.pwm_frequency = QLabel("0 Hz")
        self.pwm_duty = QLabel("0%")
        self.pwm_permission = QLabel("Waiting for IN1")
        for row, (name, value) in enumerate((
            ("State", self.pwm_state),
            ("Physical Permission", self.pwm_permission),
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
        self.self_test_progress = QProgressBar()
        self.self_test_progress.setRange(0, 10)
        self.self_test_progress.setValue(0)
        self.self_test_progress.setFormat("Built-in test: idle")
        self.self_test_result = QLabel("No built-in test run yet.")
        self.self_test_start_button = QPushButton("Start Built-In Test")
        self.self_test_start_button.setObjectName("primaryButton")
        self.self_test_cancel_button = QPushButton("Cancel Test")
        self.self_test_cancel_button.setEnabled(False)
        self.self_test_start_button.clicked.connect(
            lambda: self._self_test_requested(True)
        )
        self.self_test_cancel_button.clicked.connect(
            lambda: self._self_test_requested(False)
        )
        buttons = QHBoxLayout()
        buttons.addWidget(self.self_test_start_button)
        buttons.addWidget(self.self_test_cancel_button)
        layout.addWidget(self.loopback_result)
        layout.addWidget(self.self_test_progress)
        layout.addWidget(self.self_test_result)
        layout.addLayout(buttons)
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

    @staticmethod
    def _within_frequency_tolerance(measured, expected):
        return abs(measured - expected) <= max(2, expected * 0.02)

    def render_status(self, pwm_status, input_capture_status,
                      self_test_status=None, self_test_results=None):
        self._latest_pwm_status = dict(pwm_status)
        self._latest_capture_status = dict(input_capture_status)
        running = pwm_status["running"]
        permitted = pwm_status.get("physical_permitted", True)
        blocked = pwm_status.get("blocked", False)
        switch_valid = pwm_status.get("switch_data_valid", True)
        measured = input_capture_status["frequency_hz"]
        detected = input_capture_status["signal_detected"]
        if running:
            state_text = "Running"
        elif blocked:
            state_text = "Blocked by IN1"
        else:
            state_text = "Stopped"
        self.pwm_state.setText(state_text)
        if not switch_valid:
            permission_text = "Unavailable — PWM forced off"
        elif permitted:
            permission_text = "IN1 CLOSED — permitted"
        else:
            permission_text = "IN1 OPEN — inhibited"
        self.pwm_permission.setText(permission_text)
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

        if self_test_status is not None:
            self._render_self_test(
                self_test_status, self_test_results or []
            )

    def _render_self_test(self, status, results):
        state = status["state"]
        running = state == 1
        total = status["total_points"] or 10
        completed = len(results)
        state_name = self.SELF_TEST_STATE_NAMES.get(
            state, f"Unknown ({state})"
        )

        self.self_test_progress.setRange(0, total)
        self.self_test_progress.setValue(min(completed, total))
        permitted = self._latest_pwm_status.get(
            "physical_permitted", True
        )
        self.self_test_start_button.setEnabled(not running and permitted)
        self.self_test_cancel_button.setEnabled(running)
        self.control_group.setEnabled(not running)

        if running:
            self.self_test_progress.setFormat(
                f"Point {status['current_point']}/{total}: "
                f"{status['expected_frequency_hz']:,} Hz"
            )
            self.self_test_result.setText(
                f"Running: {status['passed_points']}/{completed} "
                "completed points passed"
            )
            return

        self.self_test_progress.setFormat(
            f"Built-in test: {state_name.lower()}"
        )
        failed = [result for result in results if not result["passed"]]
        if state == 2:
            self.self_test_progress.setValue(total)
            self.self_test_result.setText(
                f"PASS: {status['passed_points']}/{total} points"
            )
        elif state == 3:
            detail = ""
            if failed:
                first = failed[0]
                detail = (
                    f"; point {first['point']} expected "
                    f"{first['expected_frequency_hz']:,} Hz/"
                    f"{first['expected_duty_percent']}%, measured "
                    f"{first['measured_frequency_hz']:,} Hz/"
                    f"{first['measured_duty_percent']}%"
                )
            self.self_test_result.setText(
                f"FAIL: {status['passed_points']}/{total} points{detail}"
            )
        elif state == 4:
            self.self_test_result.setText("Built-in test cancelled.")
        elif state == 5:
            self.self_test_result.setText(
                "Built-in test stopped because of a firmware control error."
            )
