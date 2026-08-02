"""Queued, non-blocking UDS client built on the local ISO-TP transport."""

from .isotp_client import IsoTpClient
from .protocol import (
    UDS_DOWNLOAD_ADDRESS_LENGTH_FORMAT_IDENTIFIER,
    UDS_DOWNLOAD_DATA_FORMAT_IDENTIFIER,
    UDS_DOWNLOAD_MAX_BLOCK_LENGTH,
    UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL,
    UDS_SERVICE_READ_DATA_BY_IDENTIFIER,
    UDS_SERVICE_REQUEST_DOWNLOAD,
    UDS_SERVICE_REQUEST_TRANSFER_EXIT,
    UDS_SERVICE_ROUTINE_CONTROL,
    UDS_SERVICE_TESTER_PRESENT,
    UDS_SERVICE_TRANSFER_DATA,
)


UDS_QUEUE_CAPACITY = 16
UDS_NEGATIVE_RESPONSE_SID = 0x7F
UDS_POSITIVE_RESPONSE_OFFSET = 0x40


class UdsResult:
    def __init__(self, ok, service, payload=b"", nrc=None,
                 error_code=None, detail=""):
        self.ok = bool(ok)
        self.service = int(service)
        self.payload = bytes(payload)
        self.nrc = nrc
        self.error_code = error_code
        self.detail = detail


class UdsClient:
    def __init__(self, frame_sender, clock=None, queue_capacity=UDS_QUEUE_CAPACITY):
        if int(queue_capacity) <= 0:
            raise ValueError("UDS queue capacity must be positive")
        self.queue_capacity = int(queue_capacity)
        self._transport = IsoTpClient(frame_sender, clock=clock)
        self._queue = []
        self._active = None

    @property
    def busy(self):
        return self._active is not None or bool(self._queue)

    @property
    def stats(self):
        return dict(self._transport.stats)

    def reset(self):
        self._queue.clear()
        self._active = None
        self._transport.reset()

    def read_dids(self, dids, callback=None):
        dids = tuple(int(did) for did in dids)
        if not 1 <= len(dids) <= 3:
            raise ValueError("one UDS request supports 1..3 DIDs")
        payload = bytearray([UDS_SERVICE_READ_DATA_BY_IDENTIFIER])
        for did in dids:
            if not 0 <= did <= 0xFFFF:
                raise ValueError("DID must fit in 16 bits")
            payload.extend([(did >> 8) & 0xFF, did & 0xFF])
        return self._enqueue(payload, callback)

    def change_session(self, session, callback=None):
        session = int(session)
        if not 0 <= session <= 0x7F:
            raise ValueError("diagnostic session must fit in 7 bits")
        return self._enqueue(
            [UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL, session], callback
        )

    def tester_present(self, callback=None):
        return self._enqueue([UDS_SERVICE_TESTER_PRESENT, 0x00], callback)

    def routine_control(self, subfunction, routine_id, callback=None):
        subfunction = int(subfunction)
        routine_id = int(routine_id)
        if not 0 <= subfunction <= 0x7F:
            raise ValueError("routine subfunction must fit in 7 bits")
        if not 0 <= routine_id <= 0xFFFF:
            raise ValueError("routine identifier must fit in 16 bits")
        return self._enqueue([
            UDS_SERVICE_ROUTINE_CONTROL,
            subfunction,
            (routine_id >> 8) & 0xFF,
            routine_id & 0xFF,
        ], callback)

    def request_download(self, address, size, callback=None):
        address = int(address)
        size = int(size)
        if not 0 <= address <= 0xFFFFFFFF:
            raise ValueError("download address must fit in 32 bits")
        if not 1 <= size <= 0xFFFFFFFF:
            raise ValueError("download size must fit in 32 bits")
        request = bytearray([
            UDS_SERVICE_REQUEST_DOWNLOAD,
            UDS_DOWNLOAD_DATA_FORMAT_IDENTIFIER,
            UDS_DOWNLOAD_ADDRESS_LENGTH_FORMAT_IDENTIFIER,
        ])
        request.extend(address.to_bytes(4, "big"))
        request.extend(size.to_bytes(4, "big"))
        return self._enqueue(request, callback)

    def transfer_data(self, block_sequence, data, callback=None):
        block_sequence = int(block_sequence)
        data = bytes(data)
        max_data_length = UDS_DOWNLOAD_MAX_BLOCK_LENGTH - 2
        if not 0 <= block_sequence <= 0xFF:
            raise ValueError("transfer block sequence must fit in 8 bits")
        if not 1 <= len(data) <= max_data_length:
            raise ValueError(
                f"transfer data must contain 1..{max_data_length} bytes"
            )
        return self._enqueue(
            bytes([UDS_SERVICE_TRANSFER_DATA, block_sequence]) + data,
            callback,
        )

    def request_transfer_exit(self, callback=None):
        return self._enqueue(
            [UDS_SERVICE_REQUEST_TRANSFER_EXIT], callback
        )

    def _enqueue(self, request, callback):
        if len(self._queue) + (1 if self._active else 0) >= self.queue_capacity:
            return False
        self._queue.append({
            "payload": bytes(request),
            "callback": callback or (lambda _result: None),
        })
        self.process()
        return True

    def handle_frame(self, frame):
        consumed = self._transport.handle_frame(frame)
        self._finish_transport()
        self._start_next()
        return consumed

    def process(self):
        self._transport.process()
        self._finish_transport()
        self._start_next()

    def _start_next(self):
        if self._active is not None or not self._queue:
            return
        self._active = self._queue.pop(0)
        if not self._transport.start(self._active["payload"]):
            self._finish_transport()

    def _finish_transport(self):
        transport_result = self._transport.take_result()
        if transport_result is None or self._active is None:
            return

        request = self._active
        self._active = None
        service = request["payload"][0]
        if not transport_result.ok:
            result = UdsResult(
                False, service,
                error_code=transport_result.error_code,
                detail=transport_result.detail,
            )
        else:
            result = self._parse_response(service, transport_result.payload)
        request["callback"](result)

    @staticmethod
    def _parse_response(service, payload):
        if not payload:
            return UdsResult(
                False, service, error_code="EMPTY_RESPONSE",
                detail="UDS response has no service identifier",
            )
        if payload[0] == UDS_NEGATIVE_RESPONSE_SID:
            if len(payload) != 3 or payload[1] != service:
                return UdsResult(
                    False, service, payload=payload,
                    error_code="INVALID_NEGATIVE_RESPONSE",
                    detail="negative response does not match the request",
                )
            return UdsResult(
                False, service, payload=payload, nrc=payload[2],
                error_code="NEGATIVE_RESPONSE",
                detail=f"UDS negative response NRC=0x{payload[2]:02X}",
            )
        expected_sid = (service + UDS_POSITIVE_RESPONSE_OFFSET) & 0xFF
        if payload[0] != expected_sid:
            return UdsResult(
                False, service, payload=payload,
                error_code="SERVICE_MISMATCH",
                detail=(f"expected response SID 0x{expected_sid:02X}; "
                        f"got 0x{payload[0]:02X}"),
            )
        return UdsResult(True, service, payload=payload)
