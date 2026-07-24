import sys
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
GUI_DIRECTORY = PACKAGE_ROOT / "upload"

if not (GUI_DIRECTORY / "can_gui_app").is_dir():
    GUI_DIRECTORY = PACKAGE_ROOT

sys.path.insert(0, str(GUI_DIRECTORY))

from can_gui_app.can_app_controller import (  # noqa: E402
    CanAppController,
    SlotConfigurationError,
)
from can_gui_app.protocol import (  # noqa: E402
    CMD_LED_CONTROL,
    CMD_SET_SLOT_1,
    CMD_SET_SLOT_2,
    CMD_START_SLOT_1_COUNTER,
    CMD_START_SLOT_2_COUNTER,
    SLOT_FLAG_ENABLE,
    SLOT_FLAG_EXTENDED_ID,
    SYSTEM_STATUS_RX_ID,
)


class FakeMessage:
    def __init__(self, arbitration_id, data):
        self.arbitration_id = arbitration_id
        self.data = bytes(data)


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    sent_commands = []
    views = []
    controller = CanAppController(
        command_sender=lambda data: sent_commands.append(list(data)) or True,
        status_renderer=lambda **view: views.append(view),
    )

    controller.render_status()
    expect(views[-1]["slot_status"][1]["state"] == "Stopped",
           "slot 1 initial state is stopped")
    expect(views[-1]["led_status"] == {1: "OFF", 2: "OFF"},
           "LED initial states are off")

    result = controller.configure_and_start_slot(
        slot_no=1,
        can_id_text="0x123",
        id_type_text="Standard",
        cycle_time=10,
        counter=200,
    )
    expect(result, "valid standard slot configuration is accepted")
    expect(sent_commands[-2] == [
        CMD_SET_SLOT_1, SLOT_FLAG_ENABLE, 0x23, 0x01, 0, 0, 10, 0
    ], "slot 1 configuration payload remains byte-compatible")
    expect(sent_commands[-1] == [
        CMD_START_SLOT_1_COUNTER, 0, 200, 0, 0, 0, 0, 0
    ], "slot 1 counter payload remains byte-compatible")
    expect(controller.slot_status[1]["can_id"] == "0x123",
           "slot 1 status retains configured CAN ID")
    expect(controller.slot_status[1]["state"] == "Command sent",
           "slot status waits for the next system status frame")

    result = controller.configure_and_start_slot(
        slot_no=2,
        can_id_text="0x18FF50E5",
        id_type_text="Extended",
        cycle_time=500,
        counter=100,
    )
    expect(result, "valid extended slot configuration is accepted")
    expect(sent_commands[-2] == [
        CMD_SET_SLOT_2,
        SLOT_FLAG_ENABLE | SLOT_FLAG_EXTENDED_ID,
        0xE5, 0x50, 0xFF, 0x18, 0xF4, 0x01,
    ], "slot 2 extended ID and cycle remain little-endian")
    expect(sent_commands[-1] == [
        CMD_START_SLOT_2_COUNTER, 0, 100, 0, 0, 0, 0, 0
    ], "slot 2 counter command remains byte-compatible")

    command_count = len(sent_commands)

    try:
        controller.configure_and_start_slot(
            slot_no=1,
            can_id_text="0x800",
            id_type_text="Standard",
            cycle_time=10,
            counter=1,
        )
    except SlotConfigurationError as error:
        expect(error.title == "Invalid ID",
               "standard ID overflow keeps engineering error title")
    else:
        raise AssertionError("standard CAN ID overflow must be rejected")

    expect(len(sent_commands) == command_count,
           "invalid slot configuration does not transmit")

    expect(controller.send_led_command(2, 1), "LED command is accepted")
    expect(sent_commands[-1] == [CMD_LED_CONTROL, 2, 1, 0, 0, 0, 0, 0],
           "LED command payload remains byte-compatible")
    expect(controller.led_status[2] == "Command sent",
           "LED state waits for the next system status frame")

    handled = controller.handle_message(FakeMessage(
        SYSTEM_STATUS_RX_ID,
        [0x0B, 0, 0, 0, 0, 0, 0, 0],
    ))
    expect(handled, "0x557 is owned by the CAN application controller")
    expect(controller.slot_status[1]["state"] == "Running",
           "0x557 slot 1 flag is decoded")
    expect(controller.slot_status[2]["state"] == "Running",
           "0x557 slot 2 flag is decoded")
    expect(controller.led_status[1] == "OFF",
           "0x557 LED1 clear flag is decoded")
    expect(controller.led_status[2] == "ON",
           "0x557 LED2 set flag is decoded")

    view_count = len(views)
    expect(controller.handle_message(FakeMessage(SYSTEM_STATUS_RX_ID, [0])),
           "short 0x557 frame is consumed safely")
    expect(len(views) == view_count,
           "short 0x557 frame does not overwrite the last valid state")
    expect(not controller.handle_message(FakeMessage(0x123, [0] * 8)),
           "unrelated CAN frames remain outside the controller")

    print("PASS: GUI CAN app controller slot, LED and system status")


if __name__ == "__main__":
    main()
