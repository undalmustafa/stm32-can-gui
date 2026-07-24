"""Cross-platform CAN connection and non-blocking transport session."""

import secrets
import time

import can

from .protocol import (
    CMD_SESSION_START,
    COMMAND_ACK_ACCEPTED,
    COMMAND_ACK_ACCESS_DENIED,
    COMMAND_ACK_DUPLICATE,
    COMMAND_ACK_FLAG_ACCESS_OPEN,
    COMMAND_ACK_FLAG_EXECUTED,
    COMMAND_ACK_FLAG_SESSION_STARTED,
    COMMAND_ACK_PROTOCOL_MISMATCH,
    COMMAND_ACK_RX_ID,
    COMMAND_ACK_SESSION_REQUIRED,
    COMMAND_ACK_STATUS_NAMES,
    COMMAND_NAMES,
    GUI_COMMAND_ID_EXT,
    PROTOCOL_VERSION,
    STM32_APPLICATION_RX_IDS,
    STM32_LOG_RESPONSE_RX_ID,
    command_arbitration_id,
    u32_to_le,
)


CAN_RX_MAX_FRAMES_PER_POLL = 256
COMMAND_ACK_TIMEOUT_S = 0.75
COMMAND_ACK_MAX_RETRIES = 1
SUPPORTED_CAN_INTERFACES = {"pcan", "socketcan"}


class CanCommandResult:
    def __init__(self, ok, error_code=None, message="", sequence=None):
        self.ok = ok
        self.error_code = error_code
        self.message = message
        self.sequence = sequence


class CanSession:
    def __init__(self,
                 event_writer,
                 frame_handler,
                 error_frame_handler,
                 health_reporter,
                 bus_factory=None,
                 message_factory=None,
                 clock=None,
                 max_frames_per_poll=CAN_RX_MAX_FRAMES_PER_POLL):
        self._event_writer = event_writer
        self._frame_handler = frame_handler
        self._error_frame_handler = error_frame_handler
        self._health_reporter = health_reporter
        self._bus_factory = bus_factory or can.Bus
        self._message_factory = message_factory or can.Message
        self._clock = clock or time.monotonic
        self.max_frames_per_poll = max_frames_per_poll

        self.bus = None
        self.connected_at = None
        self.last_can_rx_time = None
        self.last_stm32_rx_time = None
        self.last_log_response_rx_time = None
        self.rx_count = 0
        self.rx_budget_hit_count = 0
        self.stm32_rx_count = 0
        self.next_command_sequence = 0
        self.command_session_nonce = 0
        self.command_session_tag = 0
        self.command_session_started = False
        self.command_session_confirmed = False
        self.command_resync_in_progress = False
        self.command_resync_queue = []
        self.pending_commands = {}
        self.command_ack_count = 0
        self.command_ack_timeout_count = 0
        self.command_retry_count = 0
        self.command_reject_count = 0

    def connect(self, interface, channel, bitrate):
        interface = str(interface).strip().lower()
        channel = str(channel).strip()
        if interface not in SUPPORTED_CAN_INTERFACES:
            raise ValueError(f"Unsupported CAN interface: {interface}")
        if not channel:
            raise ValueError("CAN channel must not be empty")

        bus_kwargs = {"interface": interface, "channel": channel}
        if interface == "pcan":
            bus_kwargs.update({"bitrate": bitrate, "auto_reset": True})

        try:
            bus = self._bus_factory(**bus_kwargs)
        except Exception:
            self.bus = None
            raise

        self.bus = bus
        self.interface = interface
        self.connected_at = self._clock()
        self.last_can_rx_time = None
        self.last_stm32_rx_time = None
        self.last_log_response_rx_time = None
        self.rx_count = 0
        self.rx_budget_hit_count = 0
        self.stm32_rx_count = 0
        self.next_command_sequence = secrets.randbelow(256)
        self.command_session_nonce = secrets.randbits(32) or 1
        self.command_session_tag = self.command_session_nonce & 0xFF
        self.command_session_started = False
        self.command_session_confirmed = False
        self.command_resync_in_progress = False
        self.command_resync_queue.clear()
        self.pending_commands.clear()
        self.command_ack_count = 0
        self.command_ack_timeout_count = 0
        self.command_retry_count = 0
        self.command_reject_count = 0

        self._event_writer(
            source="CAN",
            severity="INFO",
            event_code="CONNECTED",
            detail=(
                f"Interface={interface} Channel={channel} "
                f"Bitrate={bitrate} bit/s"
                + (" (configured by Linux)" if interface == "socketcan"
                   else "")
            ),
        )
        return bus

    def get_health_metrics(self):
        return {
            "connected_at": self.connected_at,
            "last_stm32_rx_time": self.last_stm32_rx_time,
            "last_log_response_rx_time": self.last_log_response_rx_time,
            "rx_count": self.rx_count,
            "rx_budget_hit_count": self.rx_budget_hit_count,
            "stm32_rx_count": self.stm32_rx_count,
            "command_ack_count": self.command_ack_count,
            "command_ack_timeout_count": self.command_ack_timeout_count,
            "command_retry_count": self.command_retry_count,
            "command_reject_count": self.command_reject_count,
        }

    def send_command(self, data):
        if self.bus is None:
            self._event_writer(
                source="COMMAND",
                severity="WARN",
                event_code="NOT_SENT_DISCONNECTED",
                detail="CAN adapter is not connected",
                direction="TX",
                can_id=GUI_COMMAND_ID_EXT,
                payload=data,
            )
            return CanCommandResult(
                False, "DISCONNECTED", "CAN adapter is not connected"
            )

        if len(data) != 8:
            self._event_writer(
                source="COMMAND",
                severity="FAULT",
                event_code="INVALID_DLC",
                detail=f"GUI command DLC={len(data)}; expected 8",
                direction="TX",
                can_id=GUI_COMMAND_ID_EXT,
                payload=data,
            )
            return CanCommandResult(
                False, "INVALID_DLC", "CAN data must contain 8 bytes"
            )

        if not self.command_session_started:
            session_result = self._start_command_session()
            if not session_result.ok:
                return session_result

        return self._send_reliable_command(data)

    def _allocate_sequence(self):
        for _unused in range(256):
            sequence = self.next_command_sequence
            self.next_command_sequence = (sequence + 1) & 0xFF
            if sequence not in self.pending_commands:
                return sequence

        raise RuntimeError("All command sequence values are pending")

    def _start_command_session(self):
        payload = [
            CMD_SESSION_START,
            *u32_to_le(self.command_session_nonce),
            PROTOCOL_VERSION,
            0,
            0,
        ]
        result = self._send_reliable_command(payload)
        if result.ok:
            self.command_session_started = True
        return result

    def _send_reliable_command(self, data):
        try:
            sequence = self._allocate_sequence()
        except RuntimeError as error:
            return CanCommandResult(False, "WINDOW_FULL", str(error))

        wire_id = command_arbitration_id(
            sequence, self.command_session_tag
        )
        message = self._message_factory(
            arbitration_id=wire_id,
            is_extended_id=True,
            data=bytearray(data),
            is_fd=False,
        )

        try:
            self.bus.send(message)
        except Exception as error:
            detail = f"{type(error).__name__}: {error}"
            self._event_writer(
                source="COMMAND",
                severity="FAULT",
                event_code="TX_FAILED",
                detail=detail,
                direction="TX",
                can_id=wire_id,
                payload=data,
            )
            self._health_reporter(
                "FAULT", "TX_EXCEPTION", type(error).__name__, str(error)
            )
            return CanCommandResult(False, "TX_EXCEPTION", str(error))

        command_code = data[0]
        command_name = COMMAND_NAMES.get(
            command_code,
            f"UNKNOWN_COMMAND_0x{command_code:02X}",
        )
        self._event_writer(
            source="COMMAND",
            severity="INFO",
            event_code=command_name,
            detail=(
                f"Sequence={sequence}; accepted by "
                f"{self.interface} transmit API; awaiting MCU ACK"
            ),
            direction="TX",
            can_id=wire_id,
            payload=data,
        )
        self.pending_commands[sequence] = {
            "data": list(data),
            "wire_id": wire_id,
            "sent_at": self._clock(),
            "retry_count": 0,
            "session_tag": self.command_session_tag,
        }
        return CanCommandResult(True, sequence=sequence)

    def _resend_pending(self, sequence, pending):
        message = self._message_factory(
            arbitration_id=pending["wire_id"],
            is_extended_id=True,
            data=bytearray(pending["data"]),
            is_fd=False,
        )

        try:
            self.bus.send(message)
        except Exception as error:
            self.pending_commands.pop(sequence, None)
            self._health_reporter(
                "FAULT", "TX_EXCEPTION", type(error).__name__, str(error)
            )
            return False

        pending["retry_count"] += 1
        pending["sent_at"] = self._clock()
        self.command_retry_count += 1
        self._event_writer(
            source="COMMAND",
            severity="WARN",
            event_code="ACK_RETRY",
            detail=f"Sequence={sequence}; retry={pending['retry_count']}",
            direction="TX",
            can_id=pending["wire_id"],
            payload=pending["data"],
        )
        return True

    def _handle_command_ack(self, msg):
        data = bytes(msg.data)
        if len(data) != 8 or data[0] != PROTOCOL_VERSION:
            self.command_reject_count += 1
            self._event_writer(
                source="COMMAND",
                severity="FAULT",
                event_code="INVALID_ACK",
                detail="MCU command ACK has invalid DLC or protocol version",
                direction="RX",
                can_id=COMMAND_ACK_RX_ID,
                payload=data,
            )
            return

        command = data[1]
        sequence = data[2]
        status = data[3]
        flags = data[4]
        access_seconds = data[5]
        session_tag = data[6]
        pending = self.pending_commands.get(sequence)

        if (pending is None or
                pending["data"][0] != command or
                pending["session_tag"] != session_tag):
            self.command_reject_count += 1
            self._event_writer(
                source="COMMAND",
                severity="WARN",
                event_code="UNMATCHED_ACK",
                detail=f"Command=0x{command:02X}; sequence={sequence}",
                direction="RX",
                can_id=COMMAND_ACK_RX_ID,
                payload=data,
            )
            return

        status_name = COMMAND_ACK_STATUS_NAMES.get(
            status, f"UNKNOWN_0x{status:02X}"
        )
        access_detail = (
            f"; service access open for {access_seconds}s"
            if flags & COMMAND_ACK_FLAG_ACCESS_OPEN
            else "; service access locked"
        )

        if status in {COMMAND_ACK_ACCEPTED, COMMAND_ACK_DUPLICATE}:
            self.pending_commands.pop(sequence, None)
            self.command_ack_count += 1
            if command == CMD_SESSION_START:
                self.command_session_confirmed = True
                if self.command_resync_in_progress:
                    queued_commands = list(self.command_resync_queue)
                    self.command_resync_queue.clear()
                    self.command_resync_in_progress = False
                    for queued_data in queued_commands:
                        self._send_reliable_command(queued_data)
            self._event_writer(
                source="COMMAND",
                severity="INFO",
                event_code=f"ACK_{status_name}",
                detail=(
                    f"Command={COMMAND_NAMES.get(command, hex(command))}; "
                    f"sequence={sequence}; "
                    f"executed={bool(flags & COMMAND_ACK_FLAG_EXECUTED)}; "
                    f"session={bool(flags & COMMAND_ACK_FLAG_SESSION_STARTED)}"
                    f"{access_detail}"
                ),
                direction="RX",
                can_id=COMMAND_ACK_RX_ID,
                payload=data,
            )
            return

        original_data = list(pending["data"])
        self.pending_commands.pop(sequence, None)
        self.command_reject_count += 1

        if status == COMMAND_ACK_SESSION_REQUIRED:
            if self.command_resync_in_progress:
                self.command_resync_queue.append(original_data)
                return

            retry_commands = [original_data]
            for pending_sequence, other in list(
                    self.pending_commands.items()):
                if other["data"][0] != CMD_SESSION_START:
                    retry_commands.append(list(other["data"]))
                self.pending_commands.pop(pending_sequence, None)

            self.command_session_nonce = secrets.randbits(32) or 1
            self.command_session_tag = self.command_session_nonce & 0xFF
            self.command_session_started = False
            self.command_session_confirmed = False
            self.command_resync_queue = retry_commands
            self.command_resync_in_progress = True
            if not self._start_command_session().ok:
                self.command_resync_in_progress = False
            return

        detail = (
            f"Command={COMMAND_NAMES.get(command, hex(command))}; "
            f"sequence={sequence}; status={status_name}{access_detail}"
        )
        if status == COMMAND_ACK_ACCESS_DENIED:
            detail += "; press the Nucleo B1 button to open service access"
            self._health_reporter(
                "WARN",
                "COMMAND_ACCESS_DENIED",
                "Press Nucleo B1, then retry the command",
                "State-changing commands require physical service access",
            )

        self._event_writer(
            source="COMMAND",
            severity=(
                "FAULT" if status == COMMAND_ACK_PROTOCOL_MISMATCH else "WARN"
            ),
            event_code=f"ACK_{status_name}",
            detail=detail,
            direction="RX",
            can_id=COMMAND_ACK_RX_ID,
            payload=data,
        )

    def _process_command_timeouts(self):
        now = self._clock()

        for sequence, pending in list(self.pending_commands.items()):
            if (now - pending["sent_at"]) < COMMAND_ACK_TIMEOUT_S:
                continue

            if pending["retry_count"] < COMMAND_ACK_MAX_RETRIES:
                self._resend_pending(sequence, pending)
                continue

            self.pending_commands.pop(sequence, None)
            self.command_ack_timeout_count += 1
            if pending["data"][0] == CMD_SESSION_START:
                self.command_session_started = False
                self.command_session_confirmed = False
                self.command_resync_in_progress = False
                self.command_resync_queue.clear()
            self._event_writer(
                source="COMMAND",
                severity="FAULT",
                event_code="ACK_TIMEOUT",
                detail=(
                    f"Sequence={sequence}; MCU did not acknowledge after "
                    f"{pending['retry_count']} retry"
                ),
                direction="RX",
                can_id=COMMAND_ACK_RX_ID,
                payload=pending["data"],
            )

    def poll(self):
        if self.bus is None:
            return

        processed_count = 0

        while processed_count < self.max_frames_per_poll:
            try:
                msg = self.bus.recv(timeout=0.0)
            except Exception as error:
                self._health_reporter(
                    "FAULT", "RX_EXCEPTION", type(error).__name__, str(error)
                )
                return

            if msg is None:
                break

            processed_count += 1

            if getattr(msg, "is_error_frame", False):
                self._error_frame_handler()
                continue

            now = self._clock()
            self.last_can_rx_time = now
            self.rx_count += 1

            if msg.is_extended_id:
                continue

            if msg.arbitration_id in STM32_APPLICATION_RX_IDS:
                self.last_stm32_rx_time = now
                self.stm32_rx_count += 1

                if msg.arbitration_id == COMMAND_ACK_RX_ID:
                    self._handle_command_ack(msg)
                    continue

                if msg.arbitration_id == STM32_LOG_RESPONSE_RX_ID:
                    self.last_log_response_rx_time = now

            self._frame_handler(msg)

        if processed_count == self.max_frames_per_poll:
            self.rx_budget_hit_count += 1

        self._process_command_timeouts()

    def shutdown(self):
        if self.bus is None:
            return None

        try:
            self.bus.shutdown()
        except Exception as error:
            return error

        self.pending_commands.clear()
        self.command_session_started = False
        self.command_session_confirmed = False
        self.command_resync_in_progress = False
        self.command_resync_queue.clear()
        return None
