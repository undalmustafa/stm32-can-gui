"""STM32 application-level slot and LED command/state controller."""

from .protocol import (
    CMD_LED_CONTROL,
    CMD_PWM_SET,
    CMD_PWM_SELF_TEST,
    CMD_SET_SLOT_1,
    CMD_SET_SLOT_2,
    CMD_START_SLOT_1_COUNTER,
    CMD_START_SLOT_2_COUNTER,
    SLOT_FLAG_ENABLE,
    SLOT_FLAG_EXTENDED_ID,
    SYSTEM_STATUS_RX_ID,
    PWM_STATUS_RX_ID,
    INPUT_CAPTURE_STATUS_RX_ID,
    PWM_SELF_TEST_STATUS_RX_ID,
    PWM_SELF_TEST_RESULT_RX_ID,
    parse_can_id,
    u16_to_le,
    u32_to_le,
)


class SlotConfigurationError(ValueError):
    def __init__(self, title, detail):
        super().__init__(detail)
        self.title = title
        self.detail = detail


class CanAppController:
    def __init__(self, command_sender, status_renderer,
                 pwm_status_renderer=None):
        self._command_sender = command_sender
        self._status_renderer = status_renderer
        self._pwm_status_renderer = pwm_status_renderer
        self.slot_status = {
            1: {
                "can_id": "-",
                "id_type": "-",
                "cycle_time": "-",
                "counter": "-",
                "state": "Stopped",
            },
            2: {
                "can_id": "-",
                "id_type": "-",
                "cycle_time": "-",
                "counter": "-",
                "state": "Stopped",
            },
        }
        self.led_status = {1: "OFF", 2: "OFF"}
        self.pwm_status = {
            "running": False, "frequency_hz": 0, "duty_percent": 0
        }
        self.input_capture_status = {
            "signal_detected": False,
            "frequency_hz": 0,
            "duty_percent": 0,
            "edge_count": 0,
        }
        self.pwm_self_test_status = {
            "state": 0,
            "current_point": 0,
            "total_points": 10,
            "passed_points": 0,
            "expected_frequency_hz": 0,
        }
        self.pwm_self_test_results = []

    def render_status(self):
        self._status_renderer(
            slot_status=self.slot_status,
            led_status=self.led_status,
        )

    def configure_and_start_slot(self,
                                 slot_no,
                                 can_id_text,
                                 id_type_text,
                                 cycle_time,
                                 counter):
        can_id = parse_can_id(can_id_text)
        is_extended = id_type_text == "Extended"

        if is_extended:
            if can_id > 0x1FFFFFFF:
                raise SlotConfigurationError(
                    "Invalid ID",
                    "Extended ID maksimum 0x1FFFFFFF olabilir.",
                )
        elif can_id > 0x7FF:
            raise SlotConfigurationError(
                "Invalid ID",
                "Standard ID maksimum 0x7FF olabilir.",
            )

        if slot_no == 1:
            cmd_set = CMD_SET_SLOT_1
            cmd_start = CMD_START_SLOT_1_COUNTER
        else:
            cmd_set = CMD_SET_SLOT_2
            cmd_start = CMD_START_SLOT_2_COUNTER

        flags = SLOT_FLAG_ENABLE

        if is_extended:
            flags |= SLOT_FLAG_EXTENDED_ID

        can_id_bytes = u32_to_le(can_id)
        cycle_bytes = u16_to_le(cycle_time)
        config_data = [
            cmd_set,
            flags,
            can_id_bytes[0],
            can_id_bytes[1],
            can_id_bytes[2],
            can_id_bytes[3],
            cycle_bytes[0],
            cycle_bytes[1],
        ]
        counter_bytes = u32_to_le(counter)
        start_data = [
            cmd_start,
            0x00,
            counter_bytes[0],
            counter_bytes[1],
            counter_bytes[2],
            counter_bytes[3],
            0x00,
            0x00,
        ]

        config_sent = self._command_sender(config_data)
        start_sent = self._command_sender(start_data)

        if not (config_sent and start_sent):
            return False

        self.slot_status[slot_no] = {
            "can_id": f"0x{can_id:X}",
            "id_type": id_type_text,
            "cycle_time": f"{cycle_time} ms",
            "counter": str(counter),
            "state": "Command sent",
        }
        self.render_status()
        return True

    def send_led_command(self, led_no, state):
        data = [CMD_LED_CONTROL, led_no, state, 0, 0, 0, 0, 0]

        if not self._command_sender(data):
            return False

        self.led_status[led_no] = "Command sent"
        self.render_status()
        return True

    def send_pwm_command(self, frequency_hz, duty_percent, enabled=True):
        frequency_hz = int(frequency_hz)
        duty_percent = int(duty_percent)
        if not 1 <= frequency_hz <= 1_000_000:
            raise ValueError("PWM frequency must be between 1 Hz and 1 MHz")
        if not 0 <= duty_percent <= 100:
            raise ValueError("PWM duty cycle must be between 0 and 100 percent")

        wire_frequency = frequency_hz if enabled else 0
        return self._command_sender([
            CMD_PWM_SET, *u32_to_le(wire_frequency), duty_percent, 0, 0
        ])

    def send_pwm_self_test(self, start=True):
        return self._command_sender([
            CMD_PWM_SELF_TEST, 1 if start else 0, 0, 0, 0, 0, 0, 0
        ])

    def render_pwm_status(self):
        if self._pwm_status_renderer is not None:
            self._pwm_status_renderer(
                pwm_status=self.pwm_status,
                input_capture_status=self.input_capture_status,
                self_test_status=self.pwm_self_test_status,
                self_test_results=self.pwm_self_test_results,
            )

    def handle_message(self, msg):
        if msg.arbitration_id == PWM_STATUS_RX_ID:
            data = list(msg.data)
            if len(data) >= 6:
                self.pwm_status = {
                    "running": bool(data[0]),
                    "duty_percent": data[1],
                    "frequency_hz": int.from_bytes(bytes(data[2:6]), "little"),
                }
                self.render_pwm_status()
            return True

        if msg.arbitration_id == INPUT_CAPTURE_STATUS_RX_ID:
            data = list(msg.data)
            if len(data) >= 8:
                self.input_capture_status = {
                    "signal_detected": bool(data[0]),
                    "duty_percent": data[1],
                    "frequency_hz": int.from_bytes(bytes(data[2:6]), "little"),
                    "edge_count": int.from_bytes(bytes(data[6:8]), "little"),
                }
                self.render_pwm_status()
            return True

        if msg.arbitration_id == PWM_SELF_TEST_STATUS_RX_ID:
            data = list(msg.data)
            if len(data) >= 8:
                previous_state = self.pwm_self_test_status["state"]
                state = data[0]
                if state == 1 and previous_state != 1:
                    self.pwm_self_test_results = []
                self.pwm_self_test_status = {
                    "state": state,
                    "current_point": data[1],
                    "total_points": data[2],
                    "passed_points": data[3],
                    "expected_frequency_hz": int.from_bytes(
                        bytes(data[4:8]), "little"
                    ),
                }
                self.render_pwm_status()
            return True

        if msg.arbitration_id == PWM_SELF_TEST_RESULT_RX_ID:
            data = list(msg.data)
            if len(data) >= 8:
                point = data[0]
                profile_frequencies = (
                    1_000, 10_000, 100_000, 500_000,
                    10_000, 10_000, 10_000, 10_000, 10_000, 0,
                )
                expected_frequency = (
                    profile_frequencies[point - 1]
                    if 1 <= point <= len(profile_frequencies)
                    else 0
                )
                result = {
                    "point": point,
                    "passed": bool(data[1]),
                    "expected_duty_percent": data[2],
                    "measured_duty_percent": data[3],
                    "expected_frequency_hz": expected_frequency,
                    "measured_frequency_hz": int.from_bytes(
                        bytes(data[4:8]), "little"
                    ),
                }
                self.pwm_self_test_results = [
                    item for item in self.pwm_self_test_results
                    if item["point"] != point
                ]
                self.pwm_self_test_results.append(result)
                self.pwm_self_test_results.sort(
                    key=lambda item: item["point"]
                )
                self.render_pwm_status()
            return True

        if msg.arbitration_id != SYSTEM_STATUS_RX_ID:
            return False

        data = list(msg.data)

        if len(data) < 5:
            return True

        flags = data[0]
        self.slot_status[1]["state"] = (
            "Running" if flags & 0x01 else "Stopped"
        )
        self.slot_status[2]["state"] = (
            "Running" if flags & 0x02 else "Stopped"
        )
        self.led_status[1] = "ON" if flags & 0x04 else "OFF"
        self.led_status[2] = "ON" if flags & 0x08 else "OFF"
        self.render_status()
        return True
