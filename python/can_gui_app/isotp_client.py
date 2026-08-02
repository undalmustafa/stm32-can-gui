"""Small non-blocking ISO-TP client for Classic CAN diagnostics."""

import time


ISOTP_FRAME_SIZE = 8
ISOTP_SINGLE_FRAME_CAPACITY = 7
ISOTP_DEFAULT_MAX_PAYLOAD = 512
ISOTP_DEFAULT_TIMEOUT_S = 1.0
ISOTP_DEFAULT_MAX_FLOW_CONTROL_WAIT = 3


class IsoTpResult:
    def __init__(self, ok, payload=b"", error_code=None, detail=""):
        self.ok = bool(ok)
        self.payload = bytes(payload)
        self.error_code = error_code
        self.detail = detail


class IsoTpClient:
    """Non-blocking Classic CAN ISO-TP request/response transport."""

    def __init__(self, frame_sender, clock=None,
                 timeout_s=ISOTP_DEFAULT_TIMEOUT_S,
                 max_payload=ISOTP_DEFAULT_MAX_PAYLOAD,
                 max_flow_control_wait=ISOTP_DEFAULT_MAX_FLOW_CONTROL_WAIT):
        if float(timeout_s) <= 0.0:
            raise ValueError("ISO-TP timeout must be positive")
        if int(max_payload) < ISOTP_SINGLE_FRAME_CAPACITY:
            raise ValueError("ISO-TP payload capacity is too small")
        if int(max_payload) > 0xFFF:
            raise ValueError("ISO-TP payload exceeds 12-bit addressing")
        if int(max_flow_control_wait) < 0:
            raise ValueError("ISO-TP flow-control WAIT limit is invalid")

        self._frame_sender = frame_sender
        self._clock = clock or time.monotonic
        self.timeout_s = float(timeout_s)
        self.max_payload = int(max_payload)
        self.max_flow_control_wait = int(max_flow_control_wait)
        self.stats = {
            "requests_started": 0,
            "responses_completed": 0,
            "timeouts": 0,
            "protocol_errors": 0,
            "transport_errors": 0,
            "flow_control_waits": 0,
        }
        self.reset()

    @property
    def busy(self):
        return self._state != "IDLE"

    def reset(self):
        self._state = "IDLE"
        self._deadline = None
        self._expected_length = 0
        self._expected_sequence = 1
        self._buffer = bytearray()
        self._tx_payload = b""
        self._tx_offset = 0
        self._tx_sequence = 1
        self._tx_block_size = 0
        self._tx_block_remaining = 0
        self._tx_stmin_s = 0.0
        self._tx_next_frame_at = None
        self._flow_control_wait_count = 0
        self._result = None

    def start(self, payload):
        payload = bytes(payload)
        if self.busy:
            return False
        if not 1 <= len(payload) <= self.max_payload:
            raise ValueError(
                f"ISO-TP request must contain 1..{self.max_payload} bytes"
            )

        if len(payload) <= ISOTP_SINGLE_FRAME_CAPACITY:
            frame = bytes([len(payload)]) + payload
            frame += bytes(ISOTP_FRAME_SIZE - len(frame))
            next_state = "WAIT_RESPONSE"
        else:
            frame = bytes([
                0x10 | ((len(payload) >> 8) & 0x0F),
                len(payload) & 0xFF,
            ]) + payload[:6]
            self._tx_payload = payload
            self._tx_offset = 6
            self._tx_sequence = 1
            self._flow_control_wait_count = 0
            next_state = "WAIT_FLOW_CONTROL"
        if not self._frame_sender(frame):
            self._result = IsoTpResult(
                False, error_code="TX_FAILED",
                detail="CAN transport rejected the ISO-TP request",
            )
            self.stats["transport_errors"] += 1
            return False

        self._state = next_state
        self._deadline = self._clock() + self.timeout_s
        self.stats["requests_started"] += 1
        return True

    def handle_frame(self, frame):
        if not self.busy:
            return False

        data = bytes(frame)
        if not data:
            self._fail("INVALID_FRAME", "empty ISO-TP frame")
            return True
        if len(data) > ISOTP_FRAME_SIZE:
            self._fail(
                "INVALID_FRAME", "ISO-TP frame exceeds Classic CAN DLC"
            )
            return True

        frame_type = data[0] >> 4
        if frame_type == 0x0:
            self._handle_single_frame(data)
        elif frame_type == 0x1:
            self._handle_first_frame(data)
        elif frame_type == 0x2:
            self._handle_consecutive_frame(data)
        elif frame_type == 0x3:
            self._handle_flow_control(data)
        else:
            self._fail(
                "UNEXPECTED_FRAME",
                f"unexpected ISO-TP frame type 0x{frame_type:X}",
            )
        return True

    def _handle_single_frame(self, data):
        if self._state != "WAIT_RESPONSE":
            self._fail("UNEXPECTED_FRAME", "single frame during reassembly")
            return

        length = data[0] & 0x0F
        if length == 0 or length > ISOTP_SINGLE_FRAME_CAPACITY:
            self._fail("INVALID_LENGTH", "invalid single-frame length")
            return
        if len(data) < length + 1:
            self._fail("INVALID_LENGTH", "truncated single frame")
            return
        self._complete(data[1:1 + length])

    def _handle_first_frame(self, data):
        if self._state != "WAIT_RESPONSE" or len(data) < ISOTP_FRAME_SIZE:
            self._fail("UNEXPECTED_FRAME", "invalid first frame")
            return

        length = ((data[0] & 0x0F) << 8) | data[1]
        if length <= ISOTP_SINGLE_FRAME_CAPACITY or length > self.max_payload:
            self._fail("INVALID_LENGTH", "invalid multi-frame payload length")
            return

        self._expected_length = length
        self._buffer = bytearray(data[2:ISOTP_FRAME_SIZE])
        self._expected_sequence = 1
        flow_control = bytes([0x30, 0x00, 0x00, 0, 0, 0, 0, 0])
        if not self._frame_sender(flow_control):
            self.stats["transport_errors"] += 1
            self._fail(
                "FLOW_CONTROL_TX_FAILED",
                "CAN transport rejected the ISO-TP flow-control frame",
                count_protocol_error=False,
            )
            return

        self._state = "RECEIVING"
        self._deadline = self._clock() + self.timeout_s

    def _handle_consecutive_frame(self, data):
        if self._state != "RECEIVING" or len(data) < 2:
            self._fail("UNEXPECTED_FRAME", "consecutive frame without first frame")
            return

        sequence = data[0] & 0x0F
        if sequence != self._expected_sequence:
            self._fail(
                "SEQUENCE_MISMATCH",
                f"expected sequence {self._expected_sequence}; got {sequence}",
            )
            return

        remaining = self._expected_length - len(self._buffer)
        self._buffer.extend(data[1:1 + remaining])
        self._expected_sequence = (self._expected_sequence + 1) & 0x0F
        self._deadline = self._clock() + self.timeout_s
        if len(self._buffer) >= self._expected_length:
            self._complete(self._buffer[:self._expected_length])

    def _handle_flow_control(self, data):
        if self._state != "WAIT_FLOW_CONTROL" or len(data) < 3:
            self._fail("UNEXPECTED_FRAME", "flow control without request")
            return

        flow_status = data[0] & 0x0F
        if flow_status == 0x1:
            self._flow_control_wait_count += 1
            self.stats["flow_control_waits"] += 1
            if self._flow_control_wait_count > self.max_flow_control_wait:
                self._fail(
                    "WAIT_LIMIT_EXCEEDED",
                    "ISO-TP receiver exceeded flow-control WAIT limit",
                )
                return
            self._deadline = self._clock() + self.timeout_s
            return
        if flow_status == 0x2:
            self._fail(
                "RECEIVER_OVERFLOW",
                "ISO-TP receiver rejected the request payload",
            )
            return
        if flow_status != 0x0:
            self._fail(
                "INVALID_FLOW_STATUS",
                f"invalid flow-control status 0x{flow_status:X}",
            )
            return

        stmin_s = self._decode_stmin(data[2])
        if stmin_s is None:
            self._fail(
                "INVALID_STMIN",
                f"reserved flow-control STmin 0x{data[2]:02X}",
            )
            return

        self._tx_block_size = data[1]
        self._tx_block_remaining = data[1]
        self._tx_stmin_s = stmin_s
        self._tx_next_frame_at = self._clock()
        self._state = "TRANSMITTING"
        self._deadline = self._clock() + self.timeout_s
        self._pump_tx()

    @staticmethod
    def _decode_stmin(value):
        if 0x00 <= value <= 0x7F:
            return value / 1000.0
        if 0xF1 <= value <= 0xF9:
            return (value - 0xF0) / 10000.0
        return None

    def _pump_tx(self):
        while self._state == "TRANSMITTING":
            now = self._clock()
            if now < self._tx_next_frame_at:
                return

            remaining = len(self._tx_payload) - self._tx_offset
            if remaining <= 0:
                self._state = "WAIT_RESPONSE"
                self._deadline = now + self.timeout_s
                return

            chunk_length = min(7, remaining)
            frame = bytes([0x20 | self._tx_sequence])
            frame += self._tx_payload[
                self._tx_offset:self._tx_offset + chunk_length
            ]
            frame += bytes(ISOTP_FRAME_SIZE - len(frame))
            if not self._frame_sender(frame):
                self.stats["transport_errors"] += 1
                self._fail(
                    "TX_FAILED",
                    "CAN transport rejected an ISO-TP consecutive frame",
                    count_protocol_error=False,
                )
                return

            self._tx_offset += chunk_length
            self._tx_sequence = (self._tx_sequence + 1) & 0x0F
            self._deadline = now + self.timeout_s
            if self._tx_offset >= len(self._tx_payload):
                self._state = "WAIT_RESPONSE"
                self._deadline = now + self.timeout_s
                return

            if self._tx_block_size != 0:
                self._tx_block_remaining -= 1
                if self._tx_block_remaining == 0:
                    self._state = "WAIT_FLOW_CONTROL"
                    self._deadline = now + self.timeout_s
                    return

            self._tx_next_frame_at = now + self._tx_stmin_s
            if self._tx_stmin_s > 0.0:
                return

    def _complete(self, payload):
        self._result = IsoTpResult(True, payload=payload)
        self.stats["responses_completed"] += 1
        self._state = "IDLE"
        self._deadline = None

    def _fail(self, error_code, detail, count_protocol_error=True):
        self._result = IsoTpResult(
            False, error_code=error_code, detail=detail
        )
        if count_protocol_error:
            self.stats["protocol_errors"] += 1
        self._state = "IDLE"
        self._deadline = None
        self._buffer.clear()

    def process(self):
        if self._state == "TRANSMITTING":
            self._pump_tx()
        if (self.busy and self._deadline is not None and
                self._clock() >= self._deadline):
            self.stats["timeouts"] += 1
            self._result = IsoTpResult(
                False, error_code="TIMEOUT",
                detail="ISO-TP response timeout",
            )
            self._state = "IDLE"
            self._deadline = None
            self._buffer.clear()

    def take_result(self):
        result = self._result
        self._result = None
        return result
