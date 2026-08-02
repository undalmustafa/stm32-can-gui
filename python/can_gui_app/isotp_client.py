"""Small non-blocking ISO-TP client for Classic CAN diagnostics."""

import time


ISOTP_FRAME_SIZE = 8
ISOTP_SINGLE_FRAME_CAPACITY = 7
ISOTP_DEFAULT_MAX_PAYLOAD = 512
ISOTP_DEFAULT_TIMEOUT_S = 1.0


class IsoTpResult:
    def __init__(self, ok, payload=b"", error_code=None, detail=""):
        self.ok = bool(ok)
        self.payload = bytes(payload)
        self.error_code = error_code
        self.detail = detail


class IsoTpClient:
    """Receive ISO-TP responses while the application's CAN poll keeps running.

    Current UDS requests fit a Classic CAN single frame. Responses may use
    either single-frame or first/consecutive-frame transport.
    """

    def __init__(self, frame_sender, clock=None,
                 timeout_s=ISOTP_DEFAULT_TIMEOUT_S,
                 max_payload=ISOTP_DEFAULT_MAX_PAYLOAD):
        if float(timeout_s) <= 0.0:
            raise ValueError("ISO-TP timeout must be positive")
        if int(max_payload) < ISOTP_SINGLE_FRAME_CAPACITY:
            raise ValueError("ISO-TP payload capacity is too small")

        self._frame_sender = frame_sender
        self._clock = clock or time.monotonic
        self.timeout_s = float(timeout_s)
        self.max_payload = int(max_payload)
        self.stats = {
            "requests_started": 0,
            "responses_completed": 0,
            "timeouts": 0,
            "protocol_errors": 0,
            "transport_errors": 0,
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
        self._result = None

    def start(self, payload):
        payload = bytes(payload)
        if self.busy:
            return False
        if not 1 <= len(payload) <= ISOTP_SINGLE_FRAME_CAPACITY:
            raise ValueError("ISO-TP request must contain 1..7 bytes")

        frame = bytes([len(payload)]) + payload
        frame += bytes(ISOTP_FRAME_SIZE - len(frame))
        if not self._frame_sender(frame):
            self._result = IsoTpResult(
                False, error_code="TX_FAILED",
                detail="CAN transport rejected the ISO-TP request",
            )
            self.stats["transport_errors"] += 1
            return False

        self._state = "WAIT_RESPONSE"
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
