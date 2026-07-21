"""PCAN connection, command transport and non-blocking receive session."""

import time

import can

from .protocol import (
    COMMAND_NAMES,
    GUI_COMMAND_ID_EXT,
    STM32_APPLICATION_RX_IDS,
    STM32_LOG_RESPONSE_RX_ID,
)


CAN_RX_MAX_FRAMES_PER_POLL = 256


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

        self.bus = None
        self.connected_at = None
        self.last_can_rx_time = None
        self.last_stm32_rx_time = None
        self.last_log_response_rx_time = None
        self.rx_count = 0
        self.rx_budget_hit_count = 0
        self.stm32_rx_count = 0

    def connect(self, channel, bitrate):
        try:
            bus = self._bus_factory(
                interface="pcan",
                channel=channel,
                bitrate=bitrate,
                auto_reset=True,
            )
        except Exception:
            self.bus = None
            raise

        self.bus = bus
        self.connected_at = self._clock()
        self.last_can_rx_time = None
        self.last_stm32_rx_time = None
        self.last_log_response_rx_time = None
        self.rx_count = 0
        self.rx_budget_hit_count = 0
        self.stm32_rx_count = 0

        self._event_writer(
            source="CAN",
            severity="INFO",
            event_code="CONNECTED",
            detail=f"Channel={channel} Bitrate={bitrate} bit/s",
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
            detail="GUI command accepted by PCAN transmit API",
            direction="TX",
            can_id=GUI_COMMAND_ID_EXT,
            payload=data,
        )
        return CanCommandResult(True)

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

                if msg.arbitration_id == STM32_LOG_RESPONSE_RX_ID:
                    self.last_log_response_rx_time = now

            self._frame_handler(msg)

        if processed_count == self.max_frames_per_poll:
            self.rx_budget_hit_count += 1

    def shutdown(self):
        if self.bus is None:
            return None

        try:
            self.bus.shutdown()
        except Exception as error:
            return error

        return None
