import copy
import sys
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
GUI_DIRECTORY = PACKAGE_ROOT / "upload"
if not (GUI_DIRECTORY / "can_gui_app").is_dir():
    GUI_DIRECTORY = PACKAGE_ROOT
sys.path.insert(0, str(GUI_DIRECTORY))

from can_gui_app.protocol import (  # noqa: E402
    TIC12400_ADC_RX_ID,
    TIC12400_STATUS_RX_ID,
)
from can_gui_app.tic12400_controller import Tic12400Controller  # noqa: E402


class Message:
    def __init__(self, arbitration_id, data):
        self.arbitration_id = arbitration_id
        self.data = data


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def adc_frame(generation, group, first, second, third):
    data = [generation, group]
    for code in (first, second, third):
        data.extend((code & 0xFF, (code >> 8) & 0xFF))
    return Message(TIC12400_ADC_RX_ID, data)


def main():
    rendered = []
    clock_value = [100.0]

    def render(**snapshot):
        rendered.append(copy.deepcopy(snapshot))

    controller = Tic12400Controller(
        renderer=render,
        clock=lambda: clock_value[0],
    )
    expect(
        not controller.handle_message(Message(0x123, [0] * 8)),
        "unrelated frames are not consumed",
    )

    healthy_status = Message(
        TIC12400_STATUS_RX_ID,
        [0x3F, 0x20, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00],
    )
    expect(
        controller.handle_message(healthy_status),
        "TIC12400 status frame is consumed",
    )
    status = controller.device_status
    expect(status["healthy"], "all required status flags report healthy")
    expect(status["device_id"] == 0x20, "device ID is decoded")
    expect(status["service_result_name"] == "OK",
           "service result is decoded")
    expect(status["last_nonzero_int_status"] == 8,
           "INT status is little-endian")
    expect(status["updated_at"] == 100.0,
           "status reception time is retained")

    before = copy.deepcopy(status)
    controller.handle_message(Message(TIC12400_STATUS_RX_ID, [0] * 7))
    expect(controller.device_status == before,
           "short status does not overwrite confirmed state")
    expect(controller.telemetry["malformed_frames"] == 1,
           "short status is counted as malformed")

    fault_status = Message(
        TIC12400_STATUS_RX_ID,
        [0x41, 0x20, 0x05, 0x03, 0x02, 0x00, 0x01, 0x00],
    )
    controller.handle_message(fault_status)
    status = controller.device_status
    expect(not status["healthy"], "service fault reports unhealthy")
    expect(status["service_result_name"] == "DEVICE_SPI_ERROR",
           "device SPI result name is decoded")
    expect(status["transaction_flags"]["spi_fail"],
           "SPI transaction flag is decoded")
    expect(status["transaction_flags"]["parity_fail"],
           "parity transaction flag is decoded")
    expect(status["service_failures"] == 2,
           "service-failure counter is little-endian")

    clock_value[0] = 101.0
    controller.handle_message(adc_frame(0x42, 0, 0x27, 0x03, 0x3FF))
    expect(controller.telemetry["generation"] == 0x42,
           "ADC generation is decoded")
    expect(controller.telemetry["received_group_count"] == 1,
           "first ADC group starts a partial snapshot")
    expect(controller.channels[0]["adc_code"] == 0x27,
           "IN0 raw ADC code is decoded")
    expect(controller.channels[1]["adc_code"] == 0x03,
           "IN1 raw ADC code is decoded independently")
    expect(controller.channels[2]["adc_code"] == 0x3FF,
           "10-bit full-scale ADC code is accepted")
    expect(controller.channels[0]["state"] == "UNCHARACTERIZED",
           "raw ADC is not assigned an unverified physical position")

    controller.handle_message(adc_frame(0x42, 0, 1, 2, 3))
    expect(controller.telemetry["duplicate_groups"] == 1,
           "duplicate group is diagnosed")
    expect(controller.telemetry["received_group_count"] == 1,
           "duplicate group does not inflate completion count")

    for group in range(1, 8):
        base = group * 3
        controller.handle_message(
            adc_frame(0x42, group, base, base + 1, base + 2)
        )
    expect(controller.telemetry["snapshot_complete"],
           "all eight ADC groups complete one snapshot")
    expect(controller.telemetry["complete_generation"] == 0x42,
           "complete generation is retained")
    expect(controller.channels[12]["adc_code"] is None,
           "IN12 payload data is ignored because it is not fitted")
    expect(controller.channels[12]["state"] == "NOT_FITTED",
           "IN12 remains explicitly unavailable")

    previous_in0 = controller.channels[0]["adc_code"]
    controller.handle_message(adc_frame(0x43, 7, 21, 3, 23))
    expect(not controller.telemetry["snapshot_complete"],
           "new generation resets snapshot completion")
    expect(controller.telemetry["received_group_count"] == 1,
           "new generation starts a new group mask")
    expect(controller.channels[0]["adc_code"] == previous_in0,
           "partial generation retains last confirmed channel samples")
    expect(controller.channels[22]["adc_code"] == 3,
           "IN22 code is associated with its physical channel")

    controller.handle_message(adc_frame(0x42, 1, 1, 2, 3))
    expect(controller.telemetry["stale_frames"] == 1,
           "older ADC generation is rejected")
    controller.handle_message(adc_frame(0x43, 8, 1, 2, 3))
    controller.handle_message(adc_frame(0x43, 1, 1024, 2, 3))
    expect(controller.telemetry["malformed_frames"] == 3,
           "invalid group and out-of-range ADC values are diagnosed")
    expect(len(rendered) > 0, "valid status and ADC frames are rendered")

    print("PASS: TIC12400 controller telemetry and stale-state handling")


if __name__ == "__main__":
    main()
