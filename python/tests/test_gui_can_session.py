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

    command = [0x10, 2, 1, 0, 0, 0, 0, 0]
    result = session.send_command(command)
    expect(result.ok, "valid GUI command is sent")
    expect(len(bus.sent) == 2,
           "session start precedes the first GUI command")
    expect(bus.sent[0].data[0] == protocol.CMD_SESSION_START,
           "first reliable frame starts a command session")
    expect(
        (bus.sent[-1].arbitration_id &
         protocol.GUI_COMMAND_ID_MASK_EXT) ==
        (command_id & protocol.GUI_COMMAND_ID_MASK_EXT),
        "GUI command uses the generated extended command-ID range",
    )
    expect(bus.sent[-1].is_extended_id,
           "GUI command remains an extended CAN frame")
    expect(list(bus.sent[-1].data) == command,
           "GUI command payload remains unchanged")
    expect(events[-1]["event_code"] == "LED_CONTROL",
           "known command name is logged")

    session_sequence = (
        bus.sent[0].arbitration_id & protocol.GUI_COMMAND_SEQUENCE_MASK
    )
    command_sequence = (
        bus.sent[1].arbitration_id & protocol.GUI_COMMAND_SEQUENCE_MASK
    )
    bus.rx_items.extend([
        FakeMessage(
            protocol.COMMAND_ACK_RX_ID,
            data=[
                protocol.PROTOCOL_VERSION,
                protocol.CMD_SESSION_START,
                session_sequence,
                protocol.COMMAND_ACK_ACCEPTED,
                protocol.COMMAND_ACK_FLAG_SESSION_STARTED,
                0, session.command_session_tag, 0,
            ],
        ),
        FakeMessage(
            protocol.COMMAND_ACK_RX_ID,
            data=[
                protocol.PROTOCOL_VERSION,
                command[0],
                command_sequence,
                protocol.COMMAND_ACK_ACCEPTED,
                protocol.COMMAND_ACK_FLAG_EXECUTED,
                0, session.command_session_tag, 0,
            ],
        ),
    ])
    session.poll()
    expect(session.command_session_confirmed,
           "session acknowledgement confirms reliable transport")
    expect(not session.pending_commands,
           "matching acknowledgements clear pending commands")
    expect(session.command_ack_count == 2,
           "matching MCU acknowledgements are counted")

    denied_result = session.send_command(command)
    denied_sequence = denied_result.sequence
    bus.rx_items.append(FakeMessage(
        protocol.COMMAND_ACK_RX_ID,
        data=[
            protocol.PROTOCOL_VERSION,
            command[0],
            denied_sequence,
            protocol.COMMAND_ACK_ACCESS_DENIED,
            0,
            0, session.command_session_tag, 0,
        ],
    ))
    session.poll()
    expect(health[-1][0:2] == ("WARN", "COMMAND_ACCESS_DENIED"),
           "access denial tells the operator to press B1")

    send_count = len(bus.sent)
    result = session.send_command([0x10, 1])
    expect(not result.ok and result.error_code == "INVALID_DLC",
           "invalid command DLC is rejected")
    expect(len(bus.sent) == send_count,
           "invalid command DLC does not transmit")
    expect(events[-1]["event_code"] == "INVALID_DLC",
           "invalid command DLC is logged")

    bus.send_error = RuntimeError("tx failed")
    result = session.send_command(command)
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
    expect(metrics["rx_count"] == 6,
           "all non-error CAN frames are counted")
    expect(metrics["stm32_rx_count"] == 4,
           "only known STM32 application frames update health traffic")
    expect(metrics["last_stm32_rx_time"] == 101.0,
           "STM32 receive time uses the session clock")
    expect(len(frames) == 2,
           "standard frames are delegated and extended frames are filtered")
    expect(frames[0].arbitration_id == rtc_time_id,
           "known STM32 frame reaches the application router")

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

    disconnected_bus = FakeBus()
    disconnected_session, disconnected_events, _, _, _, _ = create_session(
        session_type, disconnected_bus, clock
    )
    result = disconnected_session.send_command(command)
    expect(not result.ok and result.error_code == "DISCONNECTED",
           "command before connection is rejected")
    expect(disconnected_events[-1]["event_code"] ==
           "NOT_SENT_DISCONNECTED",
           "disconnected command attempt is logged")

    resync_bus = FakeBus()
    resync_clock = [150.0]
    resync_session, _, _, _, _, _ = create_session(
        session_type, resync_bus, resync_clock
    )
    resync_session.connect("pcan", "PCAN_USBBUS1", 500000)
    resync_result = resync_session.send_command(command)
    initial_session_sequence = (
        resync_bus.sent[0].arbitration_id &
        protocol.GUI_COMMAND_SEQUENCE_MASK
    )
    resync_bus.rx_items.append(FakeMessage(
        protocol.COMMAND_ACK_RX_ID,
        data=[
            protocol.PROTOCOL_VERSION,
            protocol.CMD_SESSION_START,
            initial_session_sequence,
            protocol.COMMAND_ACK_ACCEPTED,
            protocol.COMMAND_ACK_FLAG_SESSION_STARTED,
            0,
            resync_session.command_session_tag,
            0,
        ],
    ))
    resync_session.poll()
    resync_bus.rx_items.append(FakeMessage(
        protocol.COMMAND_ACK_RX_ID,
        data=[
            protocol.PROTOCOL_VERSION,
            command[0],
            resync_result.sequence,
            protocol.COMMAND_ACK_SESSION_REQUIRED,
            0,
            0,
            resync_session.command_session_tag,
            0,
        ],
    ))
    resync_session.poll()
    expect(resync_bus.sent[-1].data[0] == protocol.CMD_SESSION_START,
           "MCU reset response starts a replacement command session")
    replacement_sequence = (
        resync_bus.sent[-1].arbitration_id &
        protocol.GUI_COMMAND_SEQUENCE_MASK
    )
    resync_bus.rx_items.append(FakeMessage(
        protocol.COMMAND_ACK_RX_ID,
        data=[
            protocol.PROTOCOL_VERSION,
            protocol.CMD_SESSION_START,
            replacement_sequence,
            protocol.COMMAND_ACK_ACCEPTED,
            protocol.COMMAND_ACK_FLAG_SESSION_STARTED,
            0,
            resync_session.command_session_tag,
            0,
        ],
    ))
    resync_session.poll()
    expect(list(resync_bus.sent[-1].data) == command,
           "command is resent only after replacement session ACK")

    timeout_bus = FakeBus()
    timeout_clock = [200.0]
    timeout_session, _, _, _, _, _ = create_session(
        session_type, timeout_bus, timeout_clock
    )
    timeout_session.connect("pcan", "PCAN_USBBUS1", 500000)
    expect(timeout_session.send_command(command).ok,
           "timeout test sends session and command")
    timeout_clock[0] += 0.8
    timeout_session.poll()
    expect(timeout_session.command_retry_count == 2,
           "each unacknowledged reliable frame is retried once")
    timeout_clock[0] += 0.8
    timeout_session.poll()
    expect(timeout_session.command_ack_timeout_count == 2,
           "frames missing after their retry are timed out")

    print("PASS: GUI CAN session connection, TX, RX metrics and shutdown")


if __name__ == "__main__":
    main()
