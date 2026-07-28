import os
import sys
from pathlib import Path


os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
PACKAGE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE_ROOT))

try:
    from PySide6.QtWidgets import QApplication
except ImportError:
    print("SKIP: PySide6 is not installed")
    raise SystemExit(0)

from can_gui_app.pwm_panel import PwmPanel  # noqa: E402


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    app = QApplication.instance() or QApplication([])
    commands = []
    self_test_commands = []
    panel = PwmPanel(
        lambda *args: commands.append(args),
        lambda start: self_test_commands.append(start),
    )
    panel.frequency_spin.setValue(100_000)
    panel.duty_spin.setValue(40)
    panel._send(True)
    expect(commands[-1] == (100_000, 40, True),
           "panel sends the selected PWM settings")
    expect(panel._slider_to_frequency(
        panel._frequency_to_slider(10_000)) == 10_000,
        "logarithmic frequency slider round-trips presets")

    panel.render_status(
        {"running": True, "frequency_hz": 100_000, "duty_percent": 40},
        {
            "signal_detected": True,
            "frequency_hz": 99_990,
            "duty_percent": 40,
            "edge_count": 123,
        },
    )
    expect("Loopback matched:" in panel.loopback_result.text(),
           "loopback status accepts measurements within tolerance")
    expect(panel.duty_bar.value() == 40,
           "capture duty is shown by the visual indicator")

    panel.render_status(
        {
            "running": False,
            "frequency_hz": 100_000,
            "duty_percent": 40,
            "physical_permitted": True,
            "blocked": False,
            "switch_data_valid": True,
        },
        {
            "signal_detected": False,
            "frequency_hz": 0,
            "duty_percent": 0,
            "edge_count": 123,
        },
    )
    expect(
        panel.pwm_permission.text() ==
        "IN1 OPEN — remote control available",
        "open IN1 leaves PWM under remote control",
    )

    panel.render_status(
        {
            "running": False,
            "frequency_hz": 100_000,
            "duty_percent": 40,
            "physical_permitted": False,
            "blocked": True,
            "switch_data_valid": True,
        },
        {
            "signal_detected": False,
            "frequency_hz": 0,
            "duty_percent": 0,
            "edge_count": 123,
        },
    )
    expect(panel.pwm_state.text() == "Inhibited by IN1",
           "closed IN1 is shown as the active physical inhibit")

    panel.render_status(
        {"running": True, "frequency_hz": 100_000, "duty_percent": 40},
        {
            "signal_detected": True,
            "frequency_hz": 10_000,
            "duty_percent": 40,
            "edge_count": 124,
        },
    )
    expect("Loopback settling:" in panel.loopback_result.text(),
           "one stale capture sample is treated as telemetry settling")

    panel.self_test_start_button.click()
    panel.self_test_cancel_button.click()
    expect(self_test_commands == [True],
           "disabled cancel button cannot send before a test is running")

    passing_results = [
        {
            "point": point,
            "passed": True,
            "expected_duty_percent": 50,
            "measured_duty_percent": 50,
            "expected_frequency_hz": 10_000,
            "measured_frequency_hz": 10_000,
        }
        for point in range(1, 4)
    ]
    panel.render_status(
        {"running": True, "frequency_hz": 10_000, "duty_percent": 50},
        {
            "signal_detected": True,
            "frequency_hz": 10_000,
            "duty_percent": 50,
            "edge_count": 200,
        },
        {
            "state": 1,
            "current_point": 4,
            "total_points": 10,
            "passed_points": 3,
            "expected_frequency_hz": 500_000,
        },
        passing_results,
    )
    expect(panel.self_test_progress.value() == 3,
           "firmware result frames drive built-in-test progress")
    expect("Point 4/10" in panel.self_test_progress.format(),
           "running status displays the current firmware test point")
    expect(not panel.control_group.isEnabled(),
           "manual PWM controls are disabled while firmware owns the output")
    panel.self_test_cancel_button.click()
    expect(self_test_commands[-1] is False,
           "cancel button sends the firmware cancel action")

    failed_results = passing_results + [{
        "point": 4,
        "passed": False,
        "expected_duty_percent": 50,
        "measured_duty_percent": 44,
        "expected_frequency_hz": 500_000,
        "measured_frequency_hz": 555_556,
    }]
    panel.render_status(
        {"running": True, "frequency_hz": 100_000, "duty_percent": 40},
        {
            "signal_detected": True,
            "frequency_hz": 100_000,
            "duty_percent": 40,
            "edge_count": 220,
        },
        {
            "state": 3,
            "current_point": 10,
            "total_points": 10,
            "passed_points": 9,
            "expected_frequency_hz": 0,
        },
        failed_results,
    )
    expect("FAIL: 9/10" in panel.self_test_result.text(),
           "terminal firmware failure is reported")
    expect("measured 555,556 Hz/44%" in panel.self_test_result.text(),
           "the first failed point includes measured values")
    expect(panel.control_group.isEnabled(),
           "manual PWM controls return after the test finishes")
    app.processEvents()
    print("PASS: GUI PWM panel controls and built-in-test rendering")


if __name__ == "__main__":
    main()
