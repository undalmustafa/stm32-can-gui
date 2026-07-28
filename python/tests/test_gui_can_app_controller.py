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
    CMD_PWM_SET,
    CMD_PWM_SELF_TEST,
    CMD_SET_SLOT_1,
    CMD_SET_SLOT_2,
    CMD_START_SLOT_1_COUNTER,
    CMD_START_SLOT_2_COUNTER,
    PWM_CONTROL_BLOCKED,
    PWM_CONTROL_REQUESTED,
    PWM_CONTROL_SWITCH_DATA_VALID,
    SLOT_FLAG_ENABLE,
    SLOT_FLAG_EXTENDED_ID,
    SYSTEM_OVERRIDE_PWM_BLOCKED,
    SYSTEM_OVERRIDE_SLOT_1_BLOCKED,
    SYSTEM_PHYSICAL_DATA_VALID,
    SYSTEM_PHYSICAL_IN0_CLOSED,
    SYSTEM_PHYSICAL_IN1_CLOSED,
    SYSTEM_PHYSICAL_IN2_CLOSED,
    SYSTEM_REQUEST_PWM,
    SYSTEM_REQUEST_SLOT_1,
    SYSTEM_STATUS_RX_ID,
    PWM_STATUS_RX_ID,
    INPUT_CAPTURE_STATUS_RX_ID,
    PWM_SELF_TEST_STATUS_RX_ID,
    PWM_SELF_TEST_RESULT_RX_ID,
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

    expect(controller.send_pwm_command(10_000, 90),
           "valid PWM command is accepted")
    expect(sent_commands[-1] == [
        CMD_PWM_SET, 0x10, 0x27, 0, 0, 90, 0, 0
    ], "PWM command encodes frequency little-endian")
    expect(controller.send_pwm_command(10_000, 90, enabled=False),
           "PWM stop command is accepted")
    expect(sent_commands[-1] == [
        CMD_PWM_SET, 0, 0, 0, 0, 90, 0, 0
    ], "PWM stop uses the protocol frequency-zero sentinel")
    expect(controller.send_pwm_self_test(),
           "built-in PWM self-test start command is accepted")
    expect(sent_commands[-1] == [
        CMD_PWM_SELF_TEST, 1, 0, 0, 0, 0, 0, 0
    ], "built-in-test start action has a fixed eight-byte payload")
    expect(controller.send_pwm_self_test(start=False),
           "built-in PWM self-test cancel command is accepted")
    expect(sent_commands[-1] == [
        CMD_PWM_SELF_TEST, 0, 0, 0, 0, 0, 0, 0
    ], "built-in-test cancel action has a fixed eight-byte payload")

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

    expect(controller.handle_message(FakeMessage(
        SYSTEM_STATUS_RX_ID,
        [
            0x04, 0, 0, 1, 0,
            SYSTEM_REQUEST_SLOT_1 | SYSTEM_REQUEST_PWM,
            (
                SYSTEM_PHYSICAL_DATA_VALID |
                SYSTEM_PHYSICAL_IN0_CLOSED |
                SYSTEM_PHYSICAL_IN1_CLOSED |
                SYSTEM_PHYSICAL_IN2_CLOSED
            ),
            (
                SYSTEM_OVERRIDE_SLOT_1_BLOCKED |
                SYSTEM_OVERRIDE_PWM_BLOCKED
            ),
        ],
    )), "control-policy system status is consumed")
    expect(controller.slot_status[1]["state"] == "Inhibited by IN2",
           "slot status explains the physical override")
    expect(controller.control_policy["in0_closed"],
           "IN0 physical state is decoded")
    expect(controller.control_policy["pwm_blocked"],
           "PWM override state is decoded")

    view_count = len(views)
    expect(controller.handle_message(FakeMessage(SYSTEM_STATUS_RX_ID, [0])),
           "short 0x557 frame is consumed safely")
    expect(len(views) == view_count,
           "short 0x557 frame does not overwrite the last valid state")
    expect(not controller.handle_message(FakeMessage(0x123, [0] * 8)),
           "unrelated CAN frames remain outside the controller")

    pwm_views = []
    controller._pwm_status_renderer = lambda **view: pwm_views.append(view)
    expect(controller.handle_message(FakeMessage(
        PWM_STATUS_RX_ID,
        [
            0, 50, 0x10, 0x27, 0, 0,
            (
                PWM_CONTROL_REQUESTED |
                PWM_CONTROL_BLOCKED |
                PWM_CONTROL_SWITCH_DATA_VALID
            ),
            0,
        ],
    )), "PWM telemetry is consumed")
    expect(controller.pwm_status == {
        "running": False,
        "frequency_hz": 10_000,
        "duty_percent": 50,
        "requested": True,
        "physical_permitted": False,
        "blocked": True,
        "switch_data_valid": True,
    }, "PWM telemetry distinguishes requested from physically blocked")
    expect(controller.handle_message(FakeMessage(
        INPUT_CAPTURE_STATUS_RX_ID,
        [1, 49, 0x0F, 0x27, 0, 0, 0x34, 0x12]
    )), "input capture telemetry is consumed")
    expect(controller.input_capture_status == {
        "signal_detected": True,
        "frequency_hz": 9_999,
        "duty_percent": 49,
        "edge_count": 0x1234,
    }, "input capture telemetry is decoded")
    expect(len(pwm_views) == 2, "each PWM-related frame refreshes the panel")

    expect(controller.handle_message(FakeMessage(
        PWM_SELF_TEST_STATUS_RX_ID,
        [1, 3, 10, 2, 0xA0, 0x86, 0x01, 0],
    )), "built-in-test status telemetry is consumed")
    expect(controller.pwm_self_test_status == {
        "state": 1,
        "current_point": 3,
        "total_points": 10,
        "passed_points": 2,
        "expected_frequency_hz": 100_000,
    }, "built-in-test status telemetry is decoded")
    expect(controller.pwm_self_test_results == [],
           "a newly running test starts with an empty result list")

    expect(controller.handle_message(FakeMessage(
        PWM_SELF_TEST_RESULT_RX_ID,
        [3, 0, 50, 44, 0x07, 0xB2, 0x01, 0],
    )), "built-in-test point result is consumed")
    expect(controller.pwm_self_test_results == [{
        "point": 3,
        "passed": False,
        "expected_duty_percent": 50,
        "measured_duty_percent": 44,
        "expected_frequency_hz": 100_000,
        "measured_frequency_hz": 111_111,
    }], "point results combine wire measurements with the fixed test profile")
    expect(len(pwm_views) == 4,
           "status and result frames each refresh the built-in-test view")

    print("PASS: GUI CAN app controller commands and telemetry")


if __name__ == "__main__":
    main()
