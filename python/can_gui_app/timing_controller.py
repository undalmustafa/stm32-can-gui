"""Firmware DWT timing telemetry state and bounded graph history."""

from .protocol import (
    TIMING_ACK_LATENCY_RX_ID,
    TIMING_SERVICE_NAMES,
    TIMING_SERVICE_RX_ID,
    decode_timing_ack_latency,
    decode_timing_service,
)


TIMING_HISTORY_CAPACITY = 120
SPARKLINE_LEVELS = "▁▂▃▄▅▆▇█"


def build_sparkline(values, width=60):
    """Render a bounded numeric history as a compact unicode graph."""
    samples = [max(0, int(value)) for value in values][-int(width):]
    if not samples:
        return "-"

    maximum = max(samples)
    if maximum == 0:
        return SPARKLINE_LEVELS[0] * len(samples)

    last_level = len(SPARKLINE_LEVELS) - 1
    return "".join(
        SPARKLINE_LEVELS[(value * last_level) // maximum]
        for value in samples
    )


class TimingController:
    def __init__(self, renderer, history_capacity=TIMING_HISTORY_CAPACITY):
        if int(history_capacity) <= 0:
            raise ValueError("Timing history capacity must be positive")

        self._renderer = renderer
        self.history_capacity = int(history_capacity)
        self.reset()

    def reset(self):
        self.service_timings = {
            name: {
                "enabled": False,
                "current_overrun": False,
                "overrun_latched": False,
                "current_us": 0,
                "minimum_us": 0,
                "maximum_us": 0,
            }
            for name in TIMING_SERVICE_NAMES.values()
        }
        self.service_histories = {
            name: [] for name in TIMING_SERVICE_NAMES.values()
        }
        self.ack_latency = {
            "p50_us": 0,
            "p95_us": 0,
            "p99_us": 0,
            "maximum_us": 0,
        }
        self.ack_p95_history = []

    def _append_bounded(self, history, value):
        history.append(int(value))
        overflow = len(history) - self.history_capacity
        if overflow > 0:
            del history[:overflow]

    def render(self):
        self._renderer(
            service_timings={
                name: dict(values)
                for name, values in self.service_timings.items()
            },
            service_histories={
                name: list(values)
                for name, values in self.service_histories.items()
            },
            ack_latency=dict(self.ack_latency),
            ack_p95_history=list(self.ack_p95_history),
        )

    def handle_message(self, msg):
        if msg.arbitration_id == TIMING_SERVICE_RX_ID:
            try:
                timing = decode_timing_service(msg.data)
            except ValueError:
                return True

            service_name = timing.pop("service_name")
            timing.pop("service_id")
            self.service_timings[service_name] = timing
            self._append_bounded(
                self.service_histories[service_name],
                timing["current_us"],
            )
            self.render()
            return True

        if msg.arbitration_id == TIMING_ACK_LATENCY_RX_ID:
            try:
                self.ack_latency = decode_timing_ack_latency(msg.data)
            except ValueError:
                return True

            self._append_bounded(
                self.ack_p95_history,
                self.ack_latency["p95_us"],
            )
            self.render()
            return True

        return False
