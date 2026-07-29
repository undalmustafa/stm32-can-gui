import copy
import sys
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
GUI_DIRECTORY = PACKAGE_ROOT / "upload"
if not (GUI_DIRECTORY / "can_gui_app").is_dir():
    GUI_DIRECTORY = PACKAGE_ROOT
sys.path.insert(0, str(GUI_DIRECTORY))

from can_gui_app.protocol import (  # noqa: E402
    CMD_TIC12400_SET_POLARITY,
    TIC12400_PROFILE_CONFIGURATION_VALID,
    TIC12400_PROFILE_RX_ID,
    TIC12400_STATUS_RX_ID,
    TIC12400_SWITCH_DATA_VALID,
    TIC12400_SWITCH_STATE_RX_ID,
)
from can_gui_app.tic12400_controller import Tic12400Controller  # noqa: E402


class Message:
    def __init__(self, arbitration_id, data):
        self.arbitration_id = arbitration_id
        self.data = data


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def switch_frame(closed, valid, generation, flags):
    return Message(TIC12400_SWITCH_STATE_RX_ID, [
        closed & 0xFF,
        (closed >> 8) & 0xFF,
        (closed >> 16) & 0xFF,
        valid & 0xFF,
        (valid >> 8) & 0xFF,
        (valid >> 16) & 0xFF,
        generation,
        flags,
    ])


def main():
    rendered = []
    sent_commands = []
    clock_value = [100.0]

    def render(**snapshot):
        rendered.append(copy.deepcopy(snapshot))

    controller = Tic12400Controller(
        renderer=render,
        command_sender=(
            lambda data: sent_commands.append(list(data)) or True
        ),
        clock=lambda: clock_value[0],
    )
    expect(
        not controller.handle_message(Message(0x123, [0] * 8)),
        "unrelated frames are not consumed",
    )
    expect(controller.channels[0]["state"] == "UNAVAILABLE",
           "fitted channels start unavailable")
    expect(controller.channels[12]["state"] == "NOT_FITTED",
           "IN12 starts not fitted")

    healthy_status = Message(
        TIC12400_STATUS_RX_ID,
        [0x3F, 0x20, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00],
    )
    expect(controller.handle_message(healthy_status),
           "TIC12400 status frame is consumed")
    status = controller.device_status
    expect(status["healthy"], "all required status flags report healthy")
    expect(status["device_id"] == 0x20, "device ID is decoded")
    expect(status["service_result_name"] == "OK",
           "service result is decoded")
    expect(status["last_nonzero_int_status"] == 8,
           "INT status is little-endian")
    expect(status["updated_at"] == 100.0,
           "status reception time is retained")

    battery_mask = (1 << 0) | (1 << 9)
    expect(controller.handle_message(Message(
        TIC12400_PROFILE_RX_ID,
        [
            battery_mask & 0xFF,
            (battery_mask >> 8) & 0xFF,
            0,
            0xFF,
            0x03,
            0,
            3,
            TIC12400_PROFILE_CONFIGURATION_VALID,
        ],
    )), "TIC12400 applied-profile frame is consumed")
    expect(controller.profile["configuration_valid"],
           "applied profile is marked valid")
    expect(controller.channels[0]["polarity"] == "BATTERY",
           "battery-connected polarity is decoded")
    expect(controller.channels[1]["polarity"] == "GROUND",
           "ground-connected polarity is decoded")
    expect(controller.channels[12]["polarity"] == "NOT_FITTED",
           "unfitted channel has no polarity")

    expect(controller.request_polarity((1 << 1) | (1 << 8)),
           "valid polarity request is sent")
    expect(sent_commands[-1] == [
        CMD_TIC12400_SET_POLARITY,
        0x02,
        0x01,
        0,
        0,
        0,
        0,
        0,
    ], "polarity command carries the 24-bit battery mask")
    try:
        controller.request_polarity(1 << 10)
        raise AssertionError("unsupported polarity was accepted")
    except ValueError:
        pass

    before = copy.deepcopy(status)
    controller.handle_message(Message(TIC12400_STATUS_RX_ID, [0] * 7))
    expect(controller.device_status == before,
           "short status does not overwrite confirmed state")
    expect(controller.switch_state["malformed_frames"] == 1,
           "short status is counted as malformed")

    fitted_mask = 0xFFEFFF
    closed_mask = (1 << 0) | (1 << 22)
    clock_value[0] = 100.5
    expect(controller.handle_message(switch_frame(
        closed_mask,
        fitted_mask,
        5,
        TIC12400_SWITCH_DATA_VALID,
    )), "TIC12400 switch-state frame is consumed")
    expect(controller.switch_state["data_valid"],
           "switch-state validity flag is decoded")
    expect(controller.channels[0]["state"] == "CLOSED",
           "IN0 left position is decoded as closed")
    expect(controller.channels[1]["state"] == "OPEN",
           "IN1 center/right position is decoded as open")
    expect(controller.channels[12]["state"] == "NOT_FITTED",
           "IN12 remains unavailable")
    expect(controller.channels[22]["state"] == "CLOSED",
           "IN22 state uses the correct bitmap bit")

    clock_value[0] = 101.0
    controller.handle_message(switch_frame(
        closed_mask,
        fitted_mask,
        5,
        TIC12400_SWITCH_DATA_VALID,
    ))
    expect(controller.switch_state["updated_at"] == 101.0,
           "periodic same-generation state refreshes freshness")

    controller.handle_message(switch_frame(
        1 << 12,
        fitted_mask,
        6,
        TIC12400_SWITCH_DATA_VALID,
    ))
    expect(controller.switch_state["malformed_frames"] == 2,
           "closed bits outside the validity mask are rejected")
    controller.handle_message(switch_frame(
        0,
        fitted_mask,
        4,
        TIC12400_SWITCH_DATA_VALID,
    ))
    expect(controller.switch_state["stale_frames"] == 1,
           "older switch generations are rejected")

    controller.handle_message(switch_frame(
        0,
        fitted_mask,
        6,
        0,
    ))
    expect(not controller.switch_state["data_valid"],
           "invalid switch telemetry is retained as unavailable")
    expect(controller.channels[0]["state"] == "UNAVAILABLE",
           "invalid telemetry never presents a guessed open state")

    reconfigured = Tic12400Controller(
        renderer=lambda **_snapshot: None,
        clock=lambda: 200.0,
    )
    reconfigured.handle_message(Message(
        TIC12400_PROFILE_RX_ID,
        [
            0,
            0,
            0,
            0xFF,
            0x03,
            0,
            1,
            TIC12400_PROFILE_CONFIGURATION_VALID,
        ],
    ))
    reconfigured.handle_message(switch_frame(
        closed_mask,
        fitted_mask,
        49,
        TIC12400_SWITCH_DATA_VALID,
    ))
    expect(reconfigured.switch_state["generation"] == 49,
           "pre-reconfiguration generation is established")

    reconfigured.handle_message(Message(
        TIC12400_PROFILE_RX_ID,
        [
            1,
            0,
            0,
            0xFF,
            0x03,
            0,
            2,
            TIC12400_PROFILE_CONFIGURATION_VALID,
        ],
    ))
    expect(reconfigured.switch_state["generation"] is None,
           "profile change clears the old generation reference")
    expect(not reconfigured.switch_state["data_valid"],
           "old state is hidden until the new baseline arrives")

    reconfigured.handle_message(switch_frame(
        1,
        fitted_mask,
        1,
        TIC12400_SWITCH_DATA_VALID,
    ))
    expect(reconfigured.switch_state["generation"] == 1,
           "generation one is accepted after reconfiguration")
    expect(reconfigured.switch_state["stale_frames"] == 0,
           "new baseline is not rejected as stale")
    expect(reconfigured.channels[0]["state"] == "CLOSED",
           "new-profile switch state is rendered")

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
    expect(len(rendered) > 0, "valid status and state frames are rendered")

    print("PASS: TIC12400 open/closed controller and stale-state handling")


if __name__ == "__main__":
    main()
