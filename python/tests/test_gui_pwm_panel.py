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
    panel = PwmPanel(lambda *args: commands.append(args))
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

    panel.start_sweep()
    expect(commands[-1] == (1_000, 50, True),
           "sweep uses 50 percent duty at its first point")
    for frequency in panel.SWEEP_FREQUENCIES:
        panel.render_status(
            {
                "running": True,
                "frequency_hz": frequency,
                "duty_percent": 50,
            },
            {
                "signal_detected": True,
                "frequency_hz": frequency,
                "duty_percent": 50,
                "edge_count": 200,
            },
        )
        panel._advance_sweep()
    expect("PASS: 4/4" in panel.sweep_result.text(),
           "settled sweep measurements produce a complete pass report")
    expect(all(result["passed"] for result in panel._sweep_results),
           "each sweep point is evaluated once at the end of its window")
    app.processEvents()
    print("PASS: GUI PWM panel controls and loopback rendering")


if __name__ == "__main__":
    main()
