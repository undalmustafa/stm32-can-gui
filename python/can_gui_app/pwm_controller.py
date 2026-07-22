"""Oscilloscope PWM command and confirmed-state controller."""

from .protocol import (
    PWM_STATUS_RX_ID,
    build_pwm_command,
    decode_pwm_status,
)


class PwmController:
    def __init__(self, command_sender, status_renderer):
        self._command_sender = command_sender
        self._status_renderer = status_renderer
        self.status = {
            "state": "Waiting for STM32",
            "frequency_hz": None,
            "duty_percent": None,
            "result": None,
        }

    def render_status(self):
        self._status_renderer(**self.status)

    def apply(self, enabled, frequency_hz, duty_percent):
        payload = build_pwm_command(
            enabled=bool(enabled),
            frequency_hz=int(frequency_hz),
            duty_permille=round(float(duty_percent) * 10.0),
        )

        if not self._command_sender(payload):
            return False

        self.status["state"] = "Command sent"
        self.render_status()
        return True

    def handle_message(self, msg):
        if msg.arbitration_id != PWM_STATUS_RX_ID:
            return False

        try:
            decoded = decode_pwm_status(msg.data)
        except ValueError:
            return True

        result = decoded["result"]
        if result == 0:
            state = "Running" if decoded["enabled"] else "Stopped"
        elif result == 1:
            state = "Rejected settings"
        else:
            state = "Timer error"

        self.status = {
            "state": state,
            "frequency_hz": decoded["actual_frequency_hz"],
            "duty_percent": decoded["duty_permille"] / 10.0,
            "result": result,
        }
        self.render_status()
        return True
