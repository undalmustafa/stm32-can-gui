import sys
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
GUI_DIRECTORY = PACKAGE_ROOT / "upload"
if not (GUI_DIRECTORY / "can_gui_app").is_dir():
    GUI_DIRECTORY = PACKAGE_ROOT
sys.path.insert(0, str(GUI_DIRECTORY))

from can_gui_app.protocol import (  # noqa: E402
    TIMING_ACK_LATENCY_RX_ID,
    TIMING_SERVICE_RX_ID,
)
from can_gui_app.timing_controller import (  # noqa: E402
    TimingController,
    build_sparkline,
)


class FakeMessage:
    def __init__(self, arbitration_id, data):
        self.arbitration_id = arbitration_id
        self.data = bytes(data)


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    views = []
    controller = TimingController(
        renderer=lambda **view: views.append(view),
        history_capacity=3,
    )

    expect(controller.handle_message(FakeMessage(
        TIMING_SERVICE_RX_ID,
        [3, 0x05, 100, 0, 50, 0, 200, 0],
    )), "service timing frame is owned")
    rtc = controller.service_timings["RTC"]
    expect(rtc["enabled"] and rtc["overrun_latched"],
           "DWT and latched overrun flags are retained")
    expect(rtc["current_us"] == 100
           and rtc["minimum_us"] == 50
           and rtc["maximum_us"] == 200,
           "service current/min/max values are retained")

    for current_us in (110, 120, 130):
        controller.handle_message(FakeMessage(
            TIMING_SERVICE_RX_ID,
            [3, 0x01, current_us, 0, 50, 0, 200, 0],
        ))
    expect(controller.service_histories["RTC"] == [110, 120, 130],
           "service graph history is bounded to capacity")

    expect(controller.handle_message(FakeMessage(
        TIMING_ACK_LATENCY_RX_ID,
        [100, 0, 0xE8, 0x03, 0x88, 0x13, 0x30, 0x75],
    )), "ACK latency frame is owned")
    expect(controller.ack_latency == {
        "p50_us": 100,
        "p95_us": 1000,
        "p99_us": 5000,
        "maximum_us": 30000,
    }, "ACK latency percentiles are retained")
    expect(controller.ack_p95_history == [1000],
           "ACK p95 graph history is updated")
    expect(views[-1]["ack_latency"]["p99_us"] == 5000,
           "renderer receives an isolated timing snapshot")

    render_count = len(views)
    expect(controller.handle_message(FakeMessage(
        TIMING_SERVICE_RX_ID, [3, 1]
    )), "short timing frame is consumed safely")
    expect(len(views) == render_count,
           "invalid timing frame does not replace valid state")
    expect(not controller.handle_message(FakeMessage(0x123, [0] * 8)),
           "unrelated frame is not consumed")

    expect(build_sparkline([]) == "-", "empty history has a placeholder")
    expect(len(build_sparkline([0, 5, 10], width=2)) == 2,
           "sparkline rendering enforces its display width")

    controller.reset()
    expect(controller.service_histories["RTC"] == []
           and controller.ack_p95_history == [],
           "connection reset clears stale timing graphs")

    print("PASS: firmware timing telemetry, bounded history and sparklines")


if __name__ == "__main__":
    main()
