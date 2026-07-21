import sys
from pathlib import Path
from types import SimpleNamespace

from test_gui_stm32_log_sync import install_import_stubs


class FakeBus:
    def __init__(self, raw_status=0, reset_result=True):
        self.raw_status = raw_status
        self.reset_result = reset_result
        self.reset_count = 0
        self.state = SimpleNamespace(name="ACTIVE")

    def status(self):
        return self.raw_status

    def status_is_ok(self):
        return self.raw_status == 0

    def status_string(self):
        return f"PCAN_STATUS=0x{self.raw_status:08X}"

    def reset(self):
        self.reset_count += 1
        return self.reset_result


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def load_health_module():
    install_import_stubs()
    package_root = Path(__file__).resolve().parents[1]
    gui_directory = package_root / "upload"

    if not (gui_directory / "can_gui_app").is_dir():
        gui_directory = package_root

    sys.path.insert(0, str(gui_directory))

    try:
        from can_gui_app import can_health
    finally:
        sys.path.remove(str(gui_directory))

    return can_health


def create_monitor(module, bus, metrics):
    events = []
    views = []
    monitor = module.CanHealthMonitor(
        bus_provider=lambda: bus,
        metrics_provider=lambda: metrics,
        event_writer=lambda **event: events.append(event),
        view_renderer=lambda **view: views.append(view),
    )
    return monitor, events, views


def main():
    module = load_health_module()
    clock = [100.0]
    module.time.monotonic = lambda: clock[0]

    metrics = {
        "connected_at": 90.0,
        "last_stm32_rx_time": 99.9,
        "rx_count": 10,
        "rx_budget_hit_count": 0,
        "stm32_rx_count": 10,
    }
    normal_bus = FakeBus(raw_status=0)
    monitor, events, views = create_monitor(module, normal_bus, metrics)

    monitor.monitor()
    expect(views[-1]["severity"] == "OK", "healthy traffic is OK")
    expect(views[-1]["code"] == "ACTIVE", "healthy bus mode is ACTIVE")
    expect(len(events) == 1, "first health state is logged")

    monitor.monitor()
    expect(len(events) == 1, "unchanged health state is not logged again")

    sticky_bus = FakeBus(raw_status=0x00000008)
    metrics.update({
        "last_stm32_rx_time": 199.9,
        "rx_count": 20,
        "stm32_rx_count": 20,
    })
    clock[0] = 200.0
    monitor, events, views = create_monitor(module, sticky_bus, metrics)

    monitor.monitor()
    expect(views[-1]["code"] == "BUS_HEAVY",
           "BUSHEAVY initially remains a warning")
    expect(sticky_bus.reset_count == 0,
           "driver is not reset before recovered traffic is confirmed")

    metrics["stm32_rx_count"] = 23
    metrics["rx_count"] = 23
    metrics["last_stm32_rx_time"] = 200.5
    clock[0] = 200.6
    monitor.monitor()
    expect(sticky_bus.reset_count == 1,
           "one controlled reset follows confirmed recovered traffic")
    expect(any(event["event_code"] == "PCAN_RECOVERY_RESET_OK"
               for event in events),
           "successful recovery reset is logged")

    metrics["stm32_rx_count"] = 26
    metrics["rx_count"] = 26
    metrics["last_stm32_rx_time"] = 201.2
    clock[0] = 201.3
    monitor.monitor()
    expect(views[-1]["severity"] == "OK", "recovered traffic returns to OK")
    expect(views[-1]["code"] == "ACTIVE", "recovered bus returns to ACTIVE")
    expect("DRIVER_LATCHED=0x00000008" in views[-1]["detail"],
           "historical BUSHEAVY latch is reported explicitly")

    logged_after_recovery = len(events)
    monitor.monitor()
    expect(len(events) == logged_after_recovery,
           "stable recovered state is not logged repeatedly")

    error_count_before = monitor.error_frame_count
    event_count_before = len(events)

    for _ in range(20):
        monitor.observe_error_frame()

    expect(monitor.error_frame_count == error_count_before + 20,
           "all PCAN error frames are counted")
    expect(len(events) == event_count_before,
           "individual PCAN error frames do not flood the event log")

    metrics["stm32_rx_count"] = 27
    metrics["rx_count"] = 27
    metrics["last_stm32_rx_time"] = 201.4
    clock[0] = 201.5
    monitor.monitor()
    expect(views[-1]["code"] == "BUS_HEAVY",
           "a new error frame invalidates historical-latch recovery")

    print("PASS: GUI CAN health state, sticky BUSHEAVY recovery and log dedup")


if __name__ == "__main__":
    main()
