"""Sequential, fail-closed UDS firmware update workflow."""

from pathlib import Path

from .protocol import (
    UDS_ROUTINE_ERASE_INACTIVE_SLOT,
    UDS_SERVICE_REQUEST_DOWNLOAD,
    UDS_SERVICE_REQUEST_TRANSFER_EXIT,
    UDS_SERVICE_ROUTINE_CONTROL,
    UDS_SERVICE_TRANSFER_DATA,
    UDS_SESSION_DEFAULT,
    UDS_SESSION_PROGRAMMING,
)


BOOT_IMAGE_MAGIC = 0x31474D49
BOOT_IMAGE_FORMAT_VERSION = 1
BOOT_IMAGE_HEADER_SIZE = 1024
BOOT_IMAGE_MANIFEST_SIZE = 128
BOOT_IMAGE_SIGNATURE_OFFSET = 64
FLASH_PROGRAM_UNIT_SIZE = 16
ERASE_POLL_LIMIT = 256
ROUTINE_START = 0x01
ROUTINE_REQUEST_RESULTS = 0x03


class FlashController:
    def __init__(self, change_session, routine_control, request_download,
                 transfer_data, request_transfer_exit, connected_provider,
                 renderer, event_writer, file_loader=None):
        self._change_session = change_session
        self._routine_control = routine_control
        self._request_download = request_download
        self._transfer_data = transfer_data
        self._request_transfer_exit = request_transfer_exit
        self._connected_provider = connected_provider
        self._renderer = renderer
        self._event_writer = event_writer
        self._file_loader = file_loader or self._read_file
        self.reset()

    @staticmethod
    def _read_file(path):
        return Path(path).read_bytes()

    @property
    def busy(self):
        return self._busy

    def reset(self):
        self.status = "IDLE"
        self.detail = "Select a signed image artifact"
        self.progress = 0
        self.artifact_detail = "-"
        self.target_detail = "-"
        self._busy = False
        self._session_entered = False
        self._artifact = b""
        self._metadata = {}
        self._offset = 0
        self._sequence = 1
        self._block_data_size = 0
        self._current_block = b""
        self._block_retry_count = 0
        self._erase_polls = 0
        self._failure = None
        self.render()

    def render(self):
        self._renderer(
            status=self.status,
            detail=self.detail,
            progress=self.progress,
            busy=self._busy,
            artifact=self.artifact_detail,
            target=self.target_detail,
        )

    def start(self, path):
        if self._busy:
            return False
        if not self._connected_provider():
            self._finish_failure(
                "DISCONNECTED", "Connect CAN before starting an update"
            )
            return False
        try:
            artifact = bytes(self._file_loader(path))
            metadata = self._validate_artifact(artifact)
        except (OSError, ValueError) as error:
            self._finish_failure("INVALID_ARTIFACT", str(error))
            return False

        self._artifact = artifact
        self._metadata = metadata
        self._offset = 0
        self._sequence = 1
        self._erase_polls = 0
        self._failure = None
        self._busy = True
        self.status = "ENTERING PROGRAMMING SESSION"
        self.detail = "Requesting UDS programming session"
        self.progress = 1
        self.artifact_detail = (
            f"{Path(path).name} | {len(artifact)} bytes | "
            f"build {metadata['build_version']} | "
            f"security {metadata['security_counter']}"
        )
        self.target_detail = "Waiting for ECU inactive-slot selection"
        self._event_writer(
            source="FLASH",
            severity="INFO",
            event_code="UPDATE_STARTED",
            detail=self.artifact_detail,
        )
        self.render()
        if not self._change_session(
                UDS_SESSION_PROGRAMMING, self._on_programming_session):
            self._fail("QUEUE_FULL", "Programming session was not queued")
            return False
        return True

    @staticmethod
    def _validate_artifact(artifact):
        if len(artifact) < BOOT_IMAGE_HEADER_SIZE:
            raise ValueError("artifact is smaller than the 1 KiB image header")
        if (len(artifact) % FLASH_PROGRAM_UNIT_SIZE) != 0:
            raise ValueError("artifact size must be aligned to 16 bytes")
        if int.from_bytes(artifact[0:4], "little") != BOOT_IMAGE_MAGIC:
            raise ValueError("artifact has an invalid image magic")
        if (int.from_bytes(artifact[4:6], "little") !=
                BOOT_IMAGE_FORMAT_VERSION):
            raise ValueError("artifact format version is not supported")
        if (int.from_bytes(artifact[6:8], "little") !=
                BOOT_IMAGE_HEADER_SIZE):
            raise ValueError("artifact header size is not 1 KiB")

        image_size = int.from_bytes(artifact[8:12], "little")
        if image_size < 8:
            raise ValueError("artifact image payload is too small")
        payload_end = BOOT_IMAGE_HEADER_SIZE + image_size
        if payload_end > len(artifact) or len(artifact) - payload_end >= 16:
            raise ValueError("artifact payload length does not match manifest")
        digest = artifact[32:64]
        signature = artifact[
            BOOT_IMAGE_SIGNATURE_OFFSET:BOOT_IMAGE_MANIFEST_SIZE
        ]
        if digest in {bytes(32), bytes([0xFF]) * 32}:
            raise ValueError("artifact digest is empty")
        if signature in {bytes(64), bytes([0xFF]) * 64}:
            raise ValueError("artifact signature is empty")

        return {
            "image_size": image_size,
            "vector_address": int.from_bytes(artifact[12:16], "little"),
            "entry_address": int.from_bytes(artifact[16:20], "little"),
            "security_counter": int.from_bytes(
                artifact[20:24], "little"
            ),
            "build_version": int.from_bytes(artifact[24:28], "little"),
        }

    @staticmethod
    def _positive_payload(result, service, minimum_length):
        if not result.ok:
            raise ValueError(
                result.detail or result.error_code or "UDS request failed"
            )
        payload = bytes(result.payload)
        expected_sid = (service + 0x40) & 0xFF
        if len(payload) < minimum_length or payload[0] != expected_sid:
            raise ValueError(
                f"invalid positive response for SID 0x{service:02X}"
            )
        return payload

    def _on_programming_session(self, result):
        try:
            payload = self._positive_payload(result, 0x10, 2)
            if payload[1] != UDS_SESSION_PROGRAMMING:
                raise ValueError("ECU did not enter programming session")
        except ValueError as error:
            self._fail("SESSION_FAILED", str(error))
            return

        self._session_entered = True
        self.status = "ERASING INACTIVE SLOT"
        self.detail = "Starting power-loss-safe inactive-slot erase"
        self.progress = 3
        self.render()
        self._queue_erase(ROUTINE_START)

    def _queue_erase(self, subfunction):
        if not self._routine_control(
                subfunction,
                UDS_ROUTINE_ERASE_INACTIVE_SLOT,
                self._on_erase_result):
            self._fail("QUEUE_FULL", "Erase routine request was not queued")

    def _on_erase_result(self, result):
        try:
            payload = self._positive_payload(
                result, UDS_SERVICE_ROUTINE_CONTROL, 14
            )
            if (payload[1] not in {ROUTINE_START,
                                   ROUTINE_REQUEST_RESULTS} or
                    int.from_bytes(payload[2:4], "big") !=
                    UDS_ROUTINE_ERASE_INACTIVE_SLOT):
                raise ValueError("erase routine response does not match")
            routine_status = payload[4]
            slot = payload[5]
            address = int.from_bytes(payload[6:10], "big")
            capacity = int.from_bytes(payload[10:14], "big")
            if slot not in {1, 2} or capacity < BOOT_IMAGE_HEADER_SIZE:
                raise ValueError("ECU returned an invalid inactive slot")
        except ValueError as error:
            self._fail("ERASE_FAILED", str(error))
            return

        slot_name = "A" if slot == 1 else "B"
        self.target_detail = (
            f"Slot {slot_name} @ 0x{address:08X} | {capacity} bytes"
        )
        if routine_status == 1:
            self._erase_polls += 1
            if self._erase_polls > ERASE_POLL_LIMIT:
                self._fail("ERASE_TIMEOUT", "Erase routine poll limit exceeded")
                return
            self.detail = (
                f"Erasing inactive slot ({self._erase_polls}/"
                f"{ERASE_POLL_LIMIT})"
            )
            self.progress = min(9, 3 + self._erase_polls // 16)
            self.render()
            self._queue_erase(ROUTINE_REQUEST_RESULTS)
            return
        if routine_status != 0:
            self._fail("ERASE_FAILED", "ECU reported erase failure")
            return

        try:
            self._validate_target(address, capacity)
        except ValueError as error:
            self._fail("TARGET_MISMATCH", str(error))
            return

        self.status = "REQUESTING DOWNLOAD"
        self.detail = "Negotiating transfer block size"
        self.progress = 10
        self.render()
        if not self._request_download(
                address, len(self._artifact), self._on_download_requested):
            self._fail("QUEUE_FULL", "RequestDownload was not queued")

    def _validate_target(self, address, capacity):
        if len(self._artifact) > capacity:
            raise ValueError("artifact exceeds inactive-slot capacity")
        expected_vector = address + BOOT_IMAGE_HEADER_SIZE
        if self._metadata["vector_address"] != expected_vector:
            raise ValueError(
                f"artifact targets vector 0x"
                f"{self._metadata['vector_address']:08X}; ECU selected "
                f"0x{expected_vector:08X}"
            )
        entry = self._metadata["entry_address"] & ~1
        image_end = expected_vector + self._metadata["image_size"]
        if ((self._metadata["entry_address"] & 1) == 0 or
                not expected_vector <= entry < image_end):
            raise ValueError("artifact entry address is outside its payload")

    def _on_download_requested(self, result):
        try:
            payload = self._positive_payload(
                result, UDS_SERVICE_REQUEST_DOWNLOAD, 4
            )
            length_bytes = payload[1] >> 4
            if length_bytes == 0 or len(payload) != 2 + length_bytes:
                raise ValueError("invalid RequestDownload length format")
            max_block_length = int.from_bytes(
                payload[2:2 + length_bytes], "big"
            )
            data_size = min(256, max_block_length - 2)
            data_size -= data_size % FLASH_PROGRAM_UNIT_SIZE
            if data_size < FLASH_PROGRAM_UNIT_SIZE:
                raise ValueError("ECU transfer block capacity is too small")
        except ValueError as error:
            self._fail("DOWNLOAD_REJECTED", str(error))
            return

        self._block_data_size = data_size
        self.status = "TRANSFERRING"
        self.detail = "Writing signed image payload"
        self.render()
        self._send_next_block()

    def _send_next_block(self):
        if self._offset >= len(self._artifact):
            self.status = "VERIFYING"
            self.detail = "Verifying signature and scheduling pending slot"
            self.progress = 96
            self.render()
            if not self._request_transfer_exit(self._on_transfer_exit):
                self._fail("QUEUE_FULL", "RequestTransferExit was not queued")
            return

        block = self._artifact[
            self._offset:self._offset + self._block_data_size
        ]
        self._current_block = block
        self._block_retry_count = 0
        self._queue_transfer_block()

    def _queue_transfer_block(self):
        if not self._transfer_data(
                self._sequence,
                self._current_block,
                self._on_transfer_data):
            self._fail("QUEUE_FULL", "TransferData was not queued")

    def _on_transfer_data(self, result):
        if (not result.ok and self._block_retry_count == 0 and
                result.error_code in {"TIMEOUT", "TX_FAILED"}):
            self._block_retry_count = 1
            self.detail = (
                f"Retrying block 0x{self._sequence:02X} after "
                f"{result.error_code}"
            )
            self._event_writer(
                source="FLASH",
                severity="WARN",
                event_code="BLOCK_RETRY",
                detail=self.detail,
            )
            self.render()
            self._queue_transfer_block()
            return
        try:
            payload = self._positive_payload(
                result, UDS_SERVICE_TRANSFER_DATA, 2
            )
            if len(payload) != 2 or payload[1] != self._sequence:
                raise ValueError("TransferData block counter mismatch")
        except ValueError as error:
            self._fail("TRANSFER_FAILED", str(error))
            return

        self._offset = min(
            len(self._artifact), self._offset + self._block_data_size
        )
        self._sequence = (self._sequence + 1) & 0xFF
        self.progress = 10 + int(
            (self._offset * 85) / len(self._artifact)
        )
        self.detail = (
            f"Transferred {self._offset}/{len(self._artifact)} bytes"
        )
        self.render()
        self._send_next_block()

    def _on_transfer_exit(self, result):
        try:
            payload = self._positive_payload(
                result, UDS_SERVICE_REQUEST_TRANSFER_EXIT, 1
            )
            if len(payload) != 1:
                raise ValueError("invalid RequestTransferExit response")
        except ValueError as error:
            self._fail("VERIFY_FAILED", str(error))
            return

        self.status = "LEAVING PROGRAMMING SESSION"
        self.detail = "Image accepted; closing the diagnostic session"
        self.progress = 99
        self.render()
        self._session_entered = False
        if not self._change_session(
                UDS_SESSION_DEFAULT, self._on_success_cleanup):
            self._complete("Image accepted; ECU session cleanup was not queued")

    def _on_success_cleanup(self, result):
        detail = "Signed image accepted and pending boot slot scheduled"
        if not result.ok:
            detail += "; ECU will close programming session on S3 timeout"
        self._complete(detail)

    def _complete(self, detail):
        self._busy = False
        self.status = "COMPLETE"
        self.detail = detail
        self.progress = 100
        self._event_writer(
            source="FLASH",
            severity="INFO",
            event_code="UPDATE_COMPLETE",
            detail=detail,
        )
        self.render()

    def _fail(self, code, detail):
        if self._failure is not None:
            return
        self._failure = (code, detail)
        if self._session_entered:
            self.status = "ABORTING"
            self.detail = detail
            self._session_entered = False
            self.render()
            if self._change_session(
                    UDS_SESSION_DEFAULT, self._on_failure_cleanup):
                return
        self._finish_failure(code, detail)

    def _on_failure_cleanup(self, _result):
        code, detail = self._failure
        self._finish_failure(code, detail)

    def _finish_failure(self, code, detail):
        self._busy = False
        self.status = "FAULT"
        self.detail = detail
        self._event_writer(
            source="FLASH",
            severity="FAULT",
            event_code=code,
            detail=detail,
        )
        self.render()
