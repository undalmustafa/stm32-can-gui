"""Cross-platform CAN connection and fixed-ID command transport."""

import time

import can

from .protocol import (
    CAN_RX_HEALTH_RX_ID,
    COMMAND_ACK_ACCEPTED,
    COMMAND_ACK_ACCESS_DENIED,
    COMMAND_ACK_FLAG_ACCESS_OPEN,
    COMMAND_ACK_FLAG_EXECUTED,
    COMMAND_ACK_RX_ID,
    COMMAND_ACK_STATUS_NAMES,
    COMMAND_NAMES,
    DIAGNOSTIC_REQUEST_TX_ID,
    DIAGNOSTIC_RESPONSE_RX_ID,
    GUI_COMMAND_ID_EXT,
    PROTOCOL_VERSION,
    STM32_APPLICATION_RX_IDS,
    STM32_LOG_RESPONSE_RX_ID,
    command_token,
    decode_can_rx_health,
)
from .uds_client import UdsClient


CAN_RX_MAX_FRAMES_PER_POLL = 256
COMMAND_ACK_TIMEOUT_S = 0.75
COMMAND_ACK_MAX_RETRIES = 1
COMMAND_QUEUE_CAPACITY = 32
SUPPORTED_CAN_INTERFACES = {"pcan", "socketcan"}


class CanCommandResult:
    def __init__(self, ok, error_code=None, message=""):
        self.ok = ok
        self.error_code = error_code
        self.message = message


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
        self.uds_client = UdsClient(
            self._send_diagnostic_frame,
            clock=self._clock,
        )

        self.bus = None
        self.connected_at = None
        self.last_can_rx_time = None
        self.last_stm32_rx_time = None
        self.last_log_response_rx_time = None
        self.rx_count = 0
        self.rx_budget_hit_count = 0
        self.stm32_rx_count = 0
        self.pending_command = None
        self.command_queue = []
        self.command_ack_count = 0
        self.command_ack_timeout_count = 0
        self.command_retry_count = 0
        self.command_reject_count = 0
        self.mcu_can_rx_health = None

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
        self.pending_command = None
        self.command_queue.clear()
        self.command_ack_count = 0
        self.command_ack_timeout_count = 0
        self.command_retry_count = 0
        self.command_reject_count = 0
        self.mcu_can_rx_health = None
        self.uds_client.reset()

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
            "command_queue_depth": len(self.command_queue),
            "mcu_can_rx_health": self.mcu_can_rx_health,
            "uds": self.uds_client.stats,
        }

    def read_dids(self, dids, callback=None):
        if self.bus is None:
            return False
        return self.uds_client.read_dids(dids, callback)

    def change_diagnostic_session(self, session, callback=None):
        if self.bus is None:
            return False
        return self.uds_client.change_session(session, callback)

    def send_tester_present(self, callback=None):
        if self.bus is None:
            return False
        return self.uds_client.tester_present(callback)

    def _send_diagnostic_frame(self, data):
        if self.bus is None or len(data) != 8:
            return False

        message = self._message_factory(
            arbitration_id=DIAGNOSTIC_REQUEST_TX_ID,
            is_extended_id=False,
            data=bytearray(data),
            is_fd=False,
        )
        try:
            self.bus.send(message)
        except Exception as error:
            self._event_writer(
                source="UDS",
                severity="FAULT",
                event_code="TX_FAILED",
                detail=f"{type(error).__name__}: {error}",
                direction="TX",
                can_id=DIAGNOSTIC_REQUEST_TX_ID,
                payload=data,
            )
            self._health_reporter(
                "FAULT", "UDS_TX_EXCEPTION", type(error).__name__, str(error)
            )
            return False
        return True

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
                detail=f"CAN command DLC={len(data)}; expected 8",
                direction="TX",
                can_id=GUI_COMMAND_ID_EXT,
                payload=data,
            )
            return CanCommandResult(
                False, "INVALID_DLC", "CAN data must contain 8 bytes"
            )

        if self.pending_command is not None:
            if len(self.command_queue) >= COMMAND_QUEUE_CAPACITY:
                return CanCommandResult(
                    False,
                    "WINDOW_FULL",
                    "The fixed-ID command queue is full",
                )

            self.command_queue.append(list(data))
            self._event_writer(
                source="COMMAND",
                severity="INFO",
                event_code="COMMAND_QUEUED",
                detail=(
                    f"Command={COMMAND_NAMES.get(data[0], hex(data[0]))}; "
                    f"queue_depth={len(self.command_queue)}"
                ),
                direction="TX",
                can_id=GUI_COMMAND_ID_EXT,
                payload=data,
            )
            return CanCommandResult(
                True, message="Queued behind the pending command"
            )

        return self._transmit_command(data)

    def _transmit_command(self, data):
        message = self._message_factory(
            arbitration_id=GUI_COMMAND_ID_EXT,
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
                can_id=GUI_COMMAND_ID_EXT,
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
                f"Fixed ID 0x{GUI_COMMAND_ID_EXT:08X}; accepted by "
                f"{self.interface} transmit API; awaiting MCU ACK"
            ),
            direction="TX",
            can_id=GUI_COMMAND_ID_EXT,
            payload=data,
        )
        self.pending_command = {
            "data": list(data),
            "token": command_token(data),
            "sent_at": self._clock(),
            "retry_count": 0,
        }
        return CanCommandResult(True)

    def _send_next_queued(self):
        if self.pending_command is not None or not self.command_queue:
            return

        data = self.command_queue.pop(0)
        result = self._transmit_command(data)
        if not result.ok:
            self.command_queue.clear()

    def _resend_pending(self):
        pending = self.pending_command
        if pending is None:
            return False

        message = self._message_factory(
            arbitration_id=GUI_COMMAND_ID_EXT,
            is_extended_id=True,
            data=bytearray(pending["data"]),
            is_fd=False,
        )

        try:
            self.bus.send(message)
        except Exception as error:
            self.pending_command = None
            self.command_queue.clear()
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
            detail=(
                f"Command={COMMAND_NAMES.get(pending['data'][0], hex(pending['data'][0]))}; "
                f"retry={pending['retry_count']}"
            ),
            direction="TX",
            can_id=GUI_COMMAND_ID_EXT,
            payload=pending["data"],
        )
        return True

    def _handle_command_ack(self, msg):
        data = bytes(msg.data)
        if (len(data) != 8 or
                data[0] != PROTOCOL_VERSION or
                data[6] != 0 or
                data[7] != 0):
            self.command_reject_count += 1
            self._event_writer(
                source="COMMAND",
                severity="FAULT",
                event_code="INVALID_ACK",
                detail="MCU command ACK has invalid version, DLC, or reserved bytes",
                direction="RX",
                can_id=COMMAND_ACK_RX_ID,
                payload=data,
            )
            return

        command = data[1]
        status = data[3]
        flags = data[4]
        access_seconds = data[5]
        pending = self.pending_command

        if (pending is None or
                pending["data"][0] != command or
                pending["token"] != data[2]):
            self.command_reject_count += 1
            self._event_writer(
                source="COMMAND",
                severity="WARN",
                event_code="UNMATCHED_ACK",
                detail=(
                    f"Command=0x{command:02X}; "
                    f"token=0x{data[2]:02X}"
                ),
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

        self.pending_command = None
        if status == COMMAND_ACK_ACCEPTED:
            self.command_ack_count += 1
            self._event_writer(
                source="COMMAND",
                severity="INFO",
                event_code="ACK_ACCEPTED",
                detail=(
                    f"Command={COMMAND_NAMES.get(command, hex(command))}; "
                    f"executed={bool(flags & COMMAND_ACK_FLAG_EXECUTED)}"
                    f"{access_detail}"
                ),
                direction="RX",
                can_id=COMMAND_ACK_RX_ID,
                payload=data,
            )
            self._send_next_queued()
            return

        self.command_reject_count += 1
        detail = (
            f"Command={COMMAND_NAMES.get(command, hex(command))}; "
            f"status={status_name}{access_detail}"
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
            severity="WARN",
            event_code=f"ACK_{status_name}",
            detail=detail,
            direction="RX",
            can_id=COMMAND_ACK_RX_ID,
            payload=data,
        )
        self._send_next_queued()

    def _process_command_timeout(self):
        pending = self.pending_command
        if pending is None:
            return

        if (self._clock() - pending["sent_at"]) < COMMAND_ACK_TIMEOUT_S:
            return

        if pending["retry_count"] < COMMAND_ACK_MAX_RETRIES:
            self._resend_pending()
            return

        self.pending_command = None
        self.command_ack_timeout_count += 1
        self._event_writer(
            source="COMMAND",
            severity="FAULT",
            event_code="ACK_TIMEOUT",
            detail=(
                f"Command={COMMAND_NAMES.get(pending['data'][0], hex(pending['data'][0]))}; "
                f"MCU did not acknowledge after {pending['retry_count']} retry"
            ),
            direction="RX",
            can_id=COMMAND_ACK_RX_ID,
            payload=pending["data"],
        )
        self._send_next_queued()

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

                if msg.arbitration_id == DIAGNOSTIC_RESPONSE_RX_ID:
                    self.uds_client.handle_frame(msg.data)
                    continue

                if msg.arbitration_id == STM32_LOG_RESPONSE_RX_ID:
                    self.last_log_response_rx_time = now

                if msg.arbitration_id == CAN_RX_HEALTH_RX_ID:
                    try:
                        self.mcu_can_rx_health = decode_can_rx_health(
                            msg.data
                        )
                    except ValueError as error:
                        self._health_reporter(
                            "WARN",
                            "MCU_RX_HEALTH_INVALID",
                            type(error).__name__,
                            str(error),
                        )

            self._frame_handler(msg)

        if processed_count == self.max_frames_per_poll:
            self.rx_budget_hit_count += 1

        self._process_command_timeout()
        self.uds_client.process()

    def shutdown(self):
        if self.bus is None:
            return None

        try:
            self.bus.shutdown()
        except Exception as error:
            return error

        self.pending_command = None
        self.command_queue.clear()
        self.uds_client.reset()
        return None
