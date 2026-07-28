import os
import sys
from pathlib import Path


os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

try:
    from PySide6.QtWidgets import QApplication
except ImportError:
    print("SKIP: PySide6 is not installed")
    raise SystemExit(0)


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
GUI_DIRECTORY = PACKAGE_ROOT / "upload"
if not (GUI_DIRECTORY / "can_gui_app").is_dir():
    GUI_DIRECTORY = PACKAGE_ROOT
sys.path.insert(0, str(GUI_DIRECTORY))

from can_gui_app.tic12400_controller import Tic12400Controller  # noqa: E402
from can_gui_app.tic12400_panel import Tic12400Panel  # noqa: E402


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    app = QApplication.instance() or QApplication([])
    panel = Tic12400Panel(clock=lambda: 12.5)
    controller = Tic12400Controller(
        renderer=panel.render,
        clock=lambda: 12.0,
    )

    controller.device_status.update({
        "received": True,
        "healthy": True,
        "online": True,
        "configuration_valid": True,
        "crc_complete": True,
        "monitoring": True,
        "adc_characterization": True,
        "por_observed": True,
        "device_id": 0x20,
        "service_result": 0,
        "service_result_name": "OK",
        "service_failures": 0,
        "last_nonzero_int_status": 8,
        "updated_at": 12.0,
    })
    controller.channels[0].update({
        "adc_code": 0x27,
        "generation": 4,
        "updated_at": 12.0,
    })
    controller.channels[22].update({
        "adc_code": 0x03,
        "generation": 4,
        "updated_at": 12.0,
    })
    controller.telemetry.update({
        "generation": 4,
        "received_group_mask": 0xFF,
        "received_group_count": 8,
        "snapshot_complete": True,
        "complete_generation": 4,
        "last_adc_update_at": 12.0,
    })
    controller.render()
    app.processEvents()

    expect(panel.health_value.text() == "Online / Healthy",
           "healthy device state is rendered")
    expect(panel.device_id_value.text() == "0x20",
           "device ID is rendered in hexadecimal")
    expect("OK (0x00)" == panel.service_result_value.text(),
           "service result code and name are rendered")
    expect("0x027" in panel.channel_table.item(0, 3).text(),
           "IN0 raw ADC is rendered")
    expect(panel.channel_table.item(0, 4).text() == "Uncharacterized",
           "unverified physical position remains uncharacterized")
    expect(panel.channel_table.item(12, 1).text() == "Not fitted",
           "IN12 is visibly unavailable")
    expect(panel.channel_table.item(12, 3).text() == "--",
           "IN12 never displays ADC data")
    expect("0x003" in panel.channel_table.item(22, 3).text(),
           "IN22 raw ADC remains distinct from IN0")
    expect("8/8 groups" in panel.snapshot_value.text(),
           "complete segmented snapshot is rendered")

    print("PASS: TIC12400 panel raw telemetry rendering")


if __name__ == "__main__":
    main()
