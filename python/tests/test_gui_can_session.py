import sys
from pathlib import Path

from test_gui_stm32_log_sync import install_import_stubs


class FakeMessage:
    def __init__(self, arbitration_id=0, is_extended_id=False,
                 is_error_frame=False, data=None, is_fd=False):
        self.arbitration_id = arbitration_id
        self.is_extended_id = is_extended_id
        self.is_error_frame = is_error_frame
        self.data = bytes(data or [])
        self.is_fd = is_fd


class FakeBus:
    def __init__(self):
        self.sent = []
        self.rx_items = []
        self.send_error = None
        self.recv_error = None
        self.shutdown_error = None
        self.shutdown_count = 0

    def send(self, message):
        if self.send_error is not None:
            raise self.send_error
        self.sent.append(message)

    def recv(self, timeout):
        if self.recv_error is not None:
            raise self.recv_error
        if not self.rx_items:
            return None
        return self.rx_items.pop(0)

    def shutdown(self):
        self.shutdown_count += 1
        if self.shutdown_error is not None:
            raise self.shutdown_error


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def load_session_types():
    install_import_stubs()
    package_root = Path(__file__).resolve().parents[1]
    gui_directory = package_root / "upload"

    if not (gui_directory / "can_gui_app").is_dir():
        gui_directory = package_root

    sys.path.insert(0, str(gui_directory))

    try:
        from can_gui_app.can_session import CanSession
        from can_gui_app import protocol
    finally:
        sys.path.remove(str(gui_directory))

    return CanSession, protocol


def create_session(session_type, bus, clock, max_frames=256):
    events = []
    frames = []
    health_reports = []
    error_frames = []
    factory_calls = []

    def bus_factory(**kwargs):
        factory_calls.append(kwargs)
        return bus

    session = session_type(
        event_writer=lambda **event: events.append(event),
        frame_handler=lambda message: frames.append(message),
        error_frame_handler=lambda: error_frames.append(True),
        health_reporter=lambda *report: health_reports.append(report),
        bus_factory=bus_factory,
        message_factory=FakeMessage,
        clock=lambda: clock[0],
        max_frames_per_poll=max_frames,
    )
    return session, events, frames, health_reports, error_frames, factory_calls


def ack(protocol, request, status=None, flags=0, reserved=None):
    if status is None:
        status = protocol.COMMAND_ACK_ACCEPTED
    data = [
        protocol.PROTOCOL_VERSION,
        request[0],
        protocol.command_token(request),
        status,
        flags,
        0,
        0,
        0,
    ]
    if reserved is not None:
        data[6] = reserved
    return FakeMessage(protocol.COMMAND_ACK_RX_ID, data=data)


def main():
    session_type, protocol = load_session_types()
    command_id = protocol.GUI_COMMAND_ID_EXT
    rtc_time_id = protocol.RTC_TIME_RX_ID
    clock = [100.0]
    bus = FakeBus()
    session, events, frames, health, error_frames, factory_calls = (
        create_session(session_type, bus, clock)
    )

    session.connect("pcan", "PCAN_USBBUS1", 500000)
    expect(session.bus is bus, "connected PCAN bus is retained")
    expect(factory_calls[-1] == {
        "interface": "pcan",
        "channel": "PCAN_USBBUS1",
        "bitrate": 500000,
        "auto_reset": True,
    }, "PCAN connection parameters remain unchanged")
    expect(events[-1]["event_code"] == "CONNECTED",
           "successful connection is logged")
    expect(session.get_health_metrics()["connected_at"] == 100.0,
           "connection time initializes health metrics")

    socket_bus = FakeBus()
    socket_session, socket_events, _, _, _, socket_factory_calls = (
        create_session(session_type, socket_bus, clock)
    )
    socket_session.connect("socketcan", "can0", 500000)
    expect(socket_factory_calls[-1] == {
        "interface": "socketcan",
        "channel": "can0",
    }, "SocketCAN relies on the Linux interface bitrate configuration")
    expect("configured by Linux" in socket_events[-1]["detail"],
           "SocketCAN connection log explains bitrate ownership")

    led_command = [0x10, 2, 1, 0, 0, 0, 0, 0]
    pwm_command = [0x40, 0x10, 0x27, 0, 0, 50, 0, 0]
    result = session.send_command(led_command)
    expect(result.ok, "valid GUI command is transmitted")
    expect(len(bus.sent) == 1,
           "the first command is transmitted immediately")
    expect(bus.sent[0].arbitration_id == command_id,
           "the generated fixed command identifier is used exactly")
    expect(bus.sent[0].is_extended_id,
           "GUI command remains an extended CAN frame")
    expect(list(bus.sent[0].data) == led_command,
           "GUI command payload remains unchanged")

    queued = session.send_command(pwm_command)
    expect(queued.ok and len(bus.sent) == 1,
           "a second command waits while one ACK is pending")
    expect(session.get_health_metrics()["command_queue_depth"] == 1,
           "the serialized command queue depth is observable")

    bus.rx_items.append(ack(
        protocol,
        led_command,
        flags=protocol.COMMAND_ACK_FLAG_EXECUTED,
    ))
    session.poll()
    expect(len(bus.sent) == 2,
           "the next queued command transmits after the matching ACK")
    expect(bus.sent[1].arbitration_id == command_id,
           "every command reuses the same fixed identifier")
    expect(list(bus.sent[1].data) == pwm_command,
           "queued command payload is preserved")

    bus.rx_items.append(ack(
        protocol,
        pwm_command,
        flags=protocol.COMMAND_ACK_FLAG_EXECUTED,
    ))
    session.poll()
    expect(session.pending_command is None and not session.command_queue,
           "matching acknowledgements clear the fixed-ID pipeline")
    expect(session.command_ack_count == 2,
           "matching MCU acknowledgements are counted")

    expect(session.send_command(led_command).ok,
           "another command can be sent after the pipeline drains")
    bus.rx_items.append(ack(
        protocol,
        led_command,
        status=protocol.COMMAND_ACK_ACCESS_DENIED,
    ))
    session.poll()
    expect(health[-1][0:2] == ("WARN", "COMMAND_ACCESS_DENIED"),
           "access denial tells the operator to press B1")

    expect(session.send_command(led_command).ok,
           "invalid ACK test has a pending command")
    bus.rx_items.append(ack(protocol, led_command, reserved=1))
    session.poll()
    expect(session.pending_command is not None,
           "an ACK with nonzero reserved bytes cannot clear a command")
    expect(events[-1]["event_code"] == "INVALID_ACK",
           "invalid fixed-ID ACK format is logged")
    bus.rx_items.append(ack(protocol, led_command))
    session.poll()

    send_count = len(bus.sent)
    result = session.send_command([0x10, 1])
    expect(not result.ok and result.error_code == "INVALID_DLC",
           "invalid command DLC is rejected")
    expect(len(bus.sent) == send_count,
           "invalid command DLC does not transmit")
    expect(events[-1]["event_code"] == "INVALID_DLC",
           "invalid command DLC is logged")

    bus.send_error = RuntimeError("tx failed")
    result = session.send_command(led_command)
    expect(not result.ok and result.error_code == "TX_EXCEPTION",
           "PCAN TX exception is returned to the UI")
    expect(events[-1]["event_code"] == "TX_FAILED",
           "PCAN TX exception is logged")
    expect(health[-1][0:2] == ("FAULT", "TX_EXCEPTION"),
           "PCAN TX exception updates CAN health")
    bus.send_error = None

    clock[0] = 101.0
    bus.rx_items.extend([
        FakeMessage(is_error_frame=True),
        FakeMessage(0x18FF50E5, is_extended_id=True, data=[1] * 8),
        FakeMessage(rtc_time_id, data=[2] * 8),
        FakeMessage(0x123, data=[3] * 8),
    ])
    session.poll()
    metrics = session.get_health_metrics()
    expect(len(error_frames) == 1, "PCAN error frame is delegated once")
    expect(metrics["rx_count"] == 8,
           "all non-error CAN frames are counted")
    expect(metrics["stm32_rx_count"] == 6,
           "only known STM32 application frames update health traffic")
    expect(metrics["last_stm32_rx_time"] == 101.0,
           "STM32 receive time uses the session clock")
    expect(len(frames) == 2,
           "standard frames are delegated and extended frames are filtered")
    expect(frames[0].arbitration_id == rtc_time_id,
           "known STM32 frame reaches the application router")

    bus.rx_items.append(FakeMessage(
        protocol.CAN_RX_HEALTH_RX_ID,
        data=[0x07, 32, 2, 0, 3, 0, 4, 0],
    ))
    session.poll()
    mcu_rx_health = session.get_health_metrics()["mcu_can_rx_health"]
    expect(mcu_rx_health["message_lost_events"] == 2,
           "MCU RX loss telemetry is retained by the session")
    expect(mcu_rx_health["fifo_full_events"] == 3
           and mcu_rx_health["watermark_events"] == 4,
           "MCU RX pressure counters reach health metrics")

    budget_bus = FakeBus()
    budget_session, _, budget_frames, _, _, _ = create_session(
        session_type, budget_bus, clock, max_frames=2
    )
    budget_session.connect("pcan", "PCAN_USBBUS1", 500000)
    budget_bus.rx_items.extend([
        FakeMessage(rtc_time_id, data=[0] * 8),
        FakeMessage(rtc_time_id, data=[0] * 8),
        FakeMessage(rtc_time_id, data=[0] * 8),
    ])
    budget_session.poll()
    expect(len(budget_frames) == 2, "poll enforces its frame budget")
    expect(budget_session.rx_budget_hit_count == 1,
           "poll budget exhaustion is counted")

    bus.recv_error = RuntimeError("rx failed")
    session.poll()
    expect(health[-1][0:2] == ("FAULT", "RX_EXCEPTION"),
           "PCAN RX exception updates CAN health")
    bus.recv_error = None

    expect(session.shutdown() is None, "PCAN shutdown succeeds")
    expect(bus.shutdown_count == 1, "PCAN shutdown is called once")

    uds_bus = FakeBus()
    uds_session, _, uds_frames, _, _, _ = create_session(
        session_type, uds_bus, clock
    )
    uds_session.connect("pcan", "PCAN_USBBUS1", 500000)
    uds_results = []
    expect(uds_session.read_dids(
        [protocol.UDS_DID_PROTOCOL_INFO], uds_results.append
    ), "connected CAN session accepts a UDS DID request")
    expect(uds_bus.sent[-1].arbitration_id ==
           protocol.DIAGNOSTIC_REQUEST_TX_ID and
           not uds_bus.sent[-1].is_extended_id,
           "UDS requests use the generated standard CAN identifier")
    expect(bytes(uds_bus.sent[-1].data[:4]) ==
           bytes([3, 0x22, 0xF1, 0x00]),
           "CAN session transmits the ISO-TP single-frame request")
    uds_bus.rx_items.append(FakeMessage(
        protocol.DIAGNOSTIC_RESPONSE_RX_ID,
        data=[4, 0x62, 0xF1, 0x00, 0x01, 0, 0, 0],
    ))
    uds_session.poll()
    expect(uds_results[-1].ok,
           "diagnostic responses are routed to the UDS client")
    expect(not uds_frames,
           "ISO-TP transport frames do not leak into app telemetry routing")
    expect(uds_session.get_health_metrics()["stm32_rx_count"] == 1,
           "UDS responses count as known STM32 traffic")

    disconnected_bus = FakeBus()
    disconnected_session, disconnected_events, _, _, _, _ = create_session(
        session_type, disconnected_bus, clock
    )
    result = disconnected_session.send_command(led_command)
    expect(not result.ok and result.error_code == "DISCONNECTED",
           "command before connection is rejected")
    expect(disconnected_events[-1]["event_code"] ==
           "NOT_SENT_DISCONNECTED",
           "disconnected command attempt is logged")

    timeout_bus = FakeBus()
    timeout_clock = [200.0]
    timeout_session, timeout_events, _, _, _, _ = create_session(
        session_type, timeout_bus, timeout_clock
    )
    timeout_session.connect("pcan", "PCAN_USBBUS1", 500000)
    expect(timeout_session.send_command(led_command).ok,
           "timeout test sends the fixed-ID command")
    expect(timeout_session.send_command(pwm_command).ok,
           "timeout test queues the following command")
    timeout_clock[0] += 0.8
    timeout_session.poll()
    expect(timeout_session.command_retry_count == 1,
           "an unacknowledged fixed-ID command is retried once")
    expect(timeout_bus.sent[0].arbitration_id ==
           timeout_bus.sent[1].arbitration_id == command_id,
           "the retry uses the same fixed identifier")
    timeout_clock[0] += 0.8
    timeout_session.poll()
    expect(timeout_session.command_ack_timeout_count == 1,
           "a command missing after its retry is timed out")
    expect(list(timeout_bus.sent[-1].data) == pwm_command,
           "the queue continues after an ACK timeout")
    expect(any(event["event_code"] == "ACK_TIMEOUT"
               for event in timeout_events),
           "the fixed-ID ACK timeout is logged")

    print("PASS: GUI fixed-ID CAN command transport, RX metrics and shutdown")


if __name__ == "__main__":
    main()
