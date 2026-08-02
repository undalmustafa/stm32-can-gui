"""UDS diagnostic polling, DID decoding, and presentation state."""

from .protocol import (
    UDS_DID_PROTOCOL_INFO,
    UDS_DID_RESET_REASON,
    UDS_DID_RUNTIME_HEALTH,
    UDS_DID_STARTUP_HEALTH,
)


UDS_DID_LENGTHS = {
    UDS_DID_PROTOCOL_INFO: 9,
    UDS_DID_STARTUP_HEALTH: 21,
    UDS_DID_RUNTIME_HEALTH: 28,
    UDS_DID_RESET_REASON: 12,
}


def _u16(data, offset):
    return int.from_bytes(data[offset:offset + 2], "big")


def _u32(data, offset):
    return int.from_bytes(data[offset:offset + 4], "big")


def decode_read_did_response(payload):
    """Decode one positive 0x22 response containing known product DIDs."""
    payload = bytes(payload)
    if not payload or payload[0] != 0x62:
        raise ValueError("not a positive ReadDataByIdentifier response")

    decoded = {}
    offset = 1
    while offset < len(payload):
        if offset + 2 > len(payload):
            raise ValueError("truncated DID identifier")
        did = _u16(payload, offset)
        offset += 2
        length = UDS_DID_LENGTHS.get(did)
        if length is None:
            raise ValueError(f"unsupported response DID 0x{did:04X}")
        if offset + length > len(payload):
            raise ValueError(f"truncated response DID 0x{did:04X}")
        data = payload[offset:offset + length]
        offset += length
        decoded[did] = decode_did(did, data)
    return decoded


def decode_did(did, data):
    data = bytes(data)
    expected = UDS_DID_LENGTHS.get(int(did))
    if expected is None or len(data) != expected:
        raise ValueError(f"invalid data length for DID 0x{int(did):04X}")

    if did == UDS_DID_PROTOCOL_INFO:
        return {
            "uds_version": data[0],
            "protocol_version": data[1],
            "log_version": data[2],
            "isotp_capacity": _u16(data, 3),
            "request_id": _u16(data, 5),
            "response_id": _u16(data, 7),
        }
    if did == UDS_DID_STARTUP_HEALTH:
        return {
            "expected_mask": _u32(data, 0),
            "ready_mask": _u32(data, 4),
            "failed_mask": _u32(data, 8),
            "first_failed_resource": _u32(data, 12),
            "first_failure_result": _u32(data, 16),
            "degraded": bool(data[20]),
        }
    if did == UDS_DID_RUNTIME_HEALTH:
        names = (
            "uptime_ms", "latched_issue_flags", "rejected_frames_total",
            "can_rx_message_lost", "can_tx_queue_overflow",
            "isotp_protocol_errors", "isotp_transport_failures",
        )
        return {name: _u32(data, index * 4)
                for index, name in enumerate(names)}
    return {
        "decoded_flags": _u32(data, 0),
        "raw_rsr": _u32(data, 4),
        "capture_count": _u32(data, 8),
    }


class DiagnosticsController:
    POLL_BATCHES = (
        (UDS_DID_PROTOCOL_INFO, UDS_DID_STARTUP_HEALTH),
        (UDS_DID_RUNTIME_HEALTH, UDS_DID_RESET_REASON),
    )

    def __init__(self, read_dids, connected_provider, renderer,
                 event_writer):
        self._read_dids = read_dids
        self._connected_provider = connected_provider
        self._renderer = renderer
        self._event_writer = event_writer
        self.reset()

    def reset(self):
        self.values = {}
        self.pending = False
        self._batch_index = 0
        self._last_error_code = None
        self.status = "DISCONNECTED"
        self.detail = "Connect CAN to read ECU diagnostics"
        self.render()

    def render(self):
        self._renderer(
            status=self.status,
            detail=self.detail,
            values={did: dict(value) for did, value in self.values.items()},
        )

    def poll(self):
        if not self._connected_provider():
            if self.status != "DISCONNECTED":
                self.status = "DISCONNECTED"
                self.detail = "CAN connection is closed"
                self.pending = False
                self.render()
            return False
        if self.pending:
            return False

        dids = self.POLL_BATCHES[self._batch_index]
        accepted = self._read_dids(dids, self._handle_result)
        if accepted:
            self.pending = True
            self.status = "PENDING"
            self.detail = "Reading " + ", ".join(
                f"0x{did:04X}" for did in dids
            )
            self.render()
        return accepted

    def refresh(self):
        if self.pending:
            return False
        self._batch_index = 0
        return self.poll()

    def _handle_result(self, result):
        self.pending = False
        if not result.ok:
            self.status = "FAULT"
            self.detail = result.detail or result.error_code or "UDS error"
            if result.error_code != self._last_error_code:
                self._event_writer(
                    source="UDS",
                    severity="WARN",
                    event_code=result.error_code or "REQUEST_FAILED",
                    detail=self.detail,
                )
            self._last_error_code = result.error_code
            self.render()
            return

        try:
            decoded = decode_read_did_response(result.payload)
        except ValueError as error:
            self.status = "FAULT"
            self.detail = str(error)
            if self._last_error_code != "INVALID_DID_RESPONSE":
                self._event_writer(
                    source="UDS",
                    severity="WARN",
                    event_code="INVALID_DID_RESPONSE",
                    detail=self.detail,
                )
            self._last_error_code = "INVALID_DID_RESPONSE"
            self.render()
            return

        recovered = self._last_error_code is not None
        self.values.update(decoded)
        self._last_error_code = None
        self.status = "OK"
        self.detail = "ECU diagnostics are live"
        self._batch_index = (self._batch_index + 1) % len(self.POLL_BATCHES)
        if recovered:
            self._event_writer(
                source="UDS",
                severity="INFO",
                event_code="RECOVERED",
                detail="UDS diagnostic communication recovered",
            )
        self.render()
