"""STM32 RAM event-log synchronization over Classic CAN."""

import csv
import struct
import time
from datetime import datetime

import can

from .protocol import (
    CMD_LOG_GET_INFO,
    CMD_LOG_READ_SEQUENCE,
    GUI_COMMAND_ID_EXT,
    STM32_LOG_COMMIT_MARKER,
    decode_stm32_log_event_detail,
    STM32_LOG_ERROR_FRAME,
    STM32_LOG_EVENT_SYSTEM_BOOT,
    STM32_LOG_EVENT_NAMES,
    STM32_LOG_INFO_FRAGMENT_BASE,
    STM32_LOG_HEARTBEAT_FLAG_MASK,
    STM32_LOG_HEARTBEAT_FLAG_OVERWRITE,
    STM32_LOG_HEARTBEAT_FLAG_READY,
    STM32_LOG_HEARTBEAT_RX_ID,
    STM32_LOG_PROTOCOL_VERSION,
    STM32_LOG_RAM_CAPACITY,
    STM32_LOG_RECORD_FRAGMENT_BASE,
    STM32_LOG_RESPONSE_RX_ID,
    STM32_LOG_RECORD_MAGIC,
    STM32_LOG_RECORD_SIZE,
    STM32_LOG_SEVERITY_NAMES,
    STM32_LOG_SOURCE_NAMES,
)


STM32_LOG_INFO_RETRY_DELAY_S = 0.5
STM32_LOG_REQUEST_TIMEOUT_S = 1.0
STM32_LOG_MAX_RECORD_RETRIES = 3
STM32_LOG_HEARTBEAT_TIMEOUT_S = 0.35

STM32_EVENT_LOG_HEADERS = (
    "host_time_iso",
    "sequence",
    "uptime_ms",
    "rtc_epoch_s",
    "event_code",
    "event_name",
    "event_detail",
    "source",
    "severity",
    "data_0",
    "data_1",
    "crc16",
    "commit_marker",
)


class Stm32LogSync:
    def __init__(self,
                 bus_provider,
                 enabled_provider,
                 directory_provider,
                 status_changed=None,
                 record_observer=None):
        self._bus_provider = bus_provider
        self._enabled_provider = enabled_provider
        self._directory_provider = directory_provider
        self._status_changed = status_changed
        self._record_observer = record_observer

        self.file_path = None
        self.info_fragments = {}
        self.record_fragments = {}
        self.pending_kind = None
        self.pending_sequence = None
        self.request_deadline = 0.0
        self.next_info_time = 0.0
        self.info_required = True
        self.bootstrap_complete = False
        self.next_sequence = None
        self.latest_sequence = 0
        self.last_saved_sequence = 0
        self.saved_sequences = set()
        self.record_retries = {}
        self.saved_count = 0
        self.crc_error_count = 0
        self.protocol_error_count = 0
        self.timeout_count = 0
        self.missed_count = 0
        self.write_error_count = 0
        self.last_error = None
        self.last_event_summary = None
        self.last_reset_summary = None
        self.heartbeat_rx_count = 0
        self.heartbeat_missed_count = 0
        self.heartbeat_timeout_count = 0
        self.heartbeat_timeout_active = False
        self.heartbeat_wait_started = 0.0
        self.last_heartbeat_time = None
        self.last_heartbeat_alive_counter = None
        self.heartbeat_latest_sequence = 0
        self.heartbeat_record_count = 0
        self.heartbeat_ready = False
        self.heartbeat_overwrite_detected = False

    @property
    def enabled(self):
        return bool(self._enabled_provider())

    @property
    def directory(self):
        return self._directory_provider()

    @property
    def bus(self):
        return self._bus_provider()

    def _notify(self):
        if self._status_changed is not None:
            self._status_changed()

    def get_file_path(self):
        if self.file_path is None:
            file_name = datetime.now().strftime(
                "stm32_events_%Y%m%d_%H%M%S_%f.csv"
            )
            self.file_path = self.directory / file_name

        return self.file_path

    @staticmethod
    def calculate_crc16(data):
        crc = 0xFFFF

        for byte in data:
            crc ^= byte << 8

            for _ in range(8):
                if crc & 0x8000:
                    crc = ((crc << 1) ^ 0x1021) & 0xFFFF
                else:
                    crc = (crc << 1) & 0xFFFF

        return crc

    def write_record(self, record):
        if not self.enabled:
            return False

        log_path = self.get_file_path()
        event_code = record["event_code"]
        source = record["source"]
        severity = record["severity"]
        event_name = STM32_LOG_EVENT_NAMES.get(
            event_code, f"UNKNOWN_0x{event_code:04X}"
        )
        event_detail = decode_stm32_log_event_detail(
            event_code,
            record["data_0"],
            record["data_1"],
        )
        row = {
            "host_time_iso": datetime.now().astimezone().isoformat(
                timespec="milliseconds"
            ),
            "sequence": record["sequence"],
            "uptime_ms": record["uptime_ms"],
            "rtc_epoch_s": record["rtc_epoch_s"],
            "event_code": f"0x{event_code:04X}",
            "event_name": event_name,
            "event_detail": event_detail,
            "source": STM32_LOG_SOURCE_NAMES.get(
                source, f"UNKNOWN_{source}"
            ),
            "severity": STM32_LOG_SEVERITY_NAMES.get(
                severity, f"UNKNOWN_{severity}"
            ),
            "data_0": f"0x{record['data_0']:08X}",
            "data_1": f"0x{record['data_1']:08X}",
            "crc16": f"0x{record['crc16']:04X}",
            "commit_marker": f"0x{record['commit_marker']:04X}",
        }

        try:
            self.directory.mkdir(parents=True, exist_ok=True)
            write_header = not log_path.exists() or log_path.stat().st_size == 0
            file_encoding = "utf-8-sig" if write_header else "utf-8"

            with log_path.open("a", newline="", encoding=file_encoding) as file:
                writer = csv.DictWriter(
                    file, fieldnames=STM32_EVENT_LOG_HEADERS
                )

                if write_header:
                    writer.writeheader()

                writer.writerow(row)

            self.saved_count += 1
            self.last_event_summary = (
                f"#{record['sequence']} {event_name}"
                + (f" | {event_detail}" if event_detail else "")
            )
            if event_code == STM32_LOG_EVENT_SYSTEM_BOOT:
                self.last_reset_summary = event_detail
            self.last_error = None
            if self._record_observer is not None:
                self._record_observer(dict(row))
            self._notify()
            return True
        except (OSError, csv.Error) as error:
            self.write_error_count += 1
            self.last_error = f"{type(error).__name__}: {error}"
            self._notify()
            return False

    def reset(self):
        now = time.monotonic()
        self.file_path = None
        self.info_fragments.clear()
        self.record_fragments.clear()
        self.pending_kind = None
        self.pending_sequence = None
        self.request_deadline = 0.0
        self.next_info_time = now
        self.info_required = True
        self.bootstrap_complete = False
        self.next_sequence = None
        self.latest_sequence = 0
        self.last_saved_sequence = 0
        self.saved_sequences.clear()
        self.record_retries.clear()
        self.last_error = None
        self.last_event_summary = None
        self.last_reset_summary = None
        self.heartbeat_rx_count = 0
        self.heartbeat_missed_count = 0
        self.heartbeat_timeout_count = 0
        self.heartbeat_timeout_active = False
        self.heartbeat_wait_started = now
        self.last_heartbeat_time = None
        self.last_heartbeat_alive_counter = None
        self.heartbeat_latest_sequence = 0
        self.heartbeat_record_count = 0
        self.heartbeat_ready = False
        self.heartbeat_overwrite_detected = False
        self._notify()

    def send_command(self, data):
        bus = self.bus

        if bus is None or len(data) != 8:
            return False

        message = can.Message(
            arbitration_id=GUI_COMMAND_ID_EXT,
            is_extended_id=True,
            data=bytearray(data),
            is_fd=False,
        )

        try:
            bus.send(message)
            return True
        except Exception as error:
            self.protocol_error_count += 1
            self.last_error = f"TX {type(error).__name__}: {error}"
            self._notify()
            return False

    def request_info(self):
        data = [CMD_LOG_GET_INFO, 0, 0, 0, 0, 0, 0, 0]

        if not self.send_command(data):
            return False

        self.info_fragments.clear()
        self.pending_kind = "info"
        self.pending_sequence = None
        self.request_deadline = time.monotonic() + STM32_LOG_REQUEST_TIMEOUT_S
        return True

    def request_record(self, sequence):
        data = [
            CMD_LOG_READ_SEQUENCE,
            sequence & 0xFF,
            (sequence >> 8) & 0xFF,
            (sequence >> 16) & 0xFF,
            (sequence >> 24) & 0xFF,
            0,
            0,
            0,
        ]

        if not self.send_command(data):
            return False

        self.record_fragments.clear()
        self.pending_kind = "record"
        self.pending_sequence = sequence
        self.request_deadline = time.monotonic() + STM32_LOG_REQUEST_TIMEOUT_S
        return True

    def process(self):
        if self.bus is None or not self.enabled:
            return

        now = time.monotonic()
        self._update_heartbeat_timeout(now)

        if self.pending_kind is not None:
            if now >= self.request_deadline:
                timed_out_kind = self.pending_kind
                self.timeout_count += 1
                self.last_error = f"{timed_out_kind} response timeout"
                self.pending_kind = None
                self.pending_sequence = None
                self.info_fragments.clear()
                self.record_fragments.clear()
                if timed_out_kind == "info":
                    self.info_required = True
                self.next_info_time = now + STM32_LOG_INFO_RETRY_DELAY_S
                self._notify()
            return

        if self.info_required:
            if now >= self.next_info_time:
                self.request_info()
                self.next_info_time = now + STM32_LOG_INFO_RETRY_DELAY_S
            return

        if (self.next_sequence is not None and
                self.next_sequence <= self.latest_sequence):
            self.request_record(self.next_sequence)
            return

    def handle_message(self, msg):
        data = bytes(msg.data)
        message_id = getattr(
            msg, "arbitration_id", STM32_LOG_RESPONSE_RX_ID
        )

        if len(data) != 8:
            self.protocol_error_count += 1
            self.last_error = (
                f"Invalid 0x{message_id:X} DLC={len(data)}"
            )
            self._notify()
            return

        if message_id == STM32_LOG_HEARTBEAT_RX_ID:
            self._handle_heartbeat(data)
            return

        if message_id != STM32_LOG_RESPONSE_RX_ID:
            self.protocol_error_count += 1
            self.last_error = f"Unexpected STM32 log ID=0x{message_id:X}"
            self._notify()
            return

        header = data[0]

        if (STM32_LOG_INFO_FRAGMENT_BASE <= header <=
                STM32_LOG_INFO_FRAGMENT_BASE + 2):
            if self.pending_kind != "info":
                return

            fragment_index = header - STM32_LOG_INFO_FRAGMENT_BASE
            self.info_fragments[fragment_index] = data[1:]

            if len(self.info_fragments) == 3:
                self._complete_info()
            return

        if (STM32_LOG_RECORD_FRAGMENT_BASE <= header <=
                STM32_LOG_RECORD_FRAGMENT_BASE + 4):
            if self.pending_kind != "record":
                return

            fragment_index = header - STM32_LOG_RECORD_FRAGMENT_BASE
            self.record_fragments[fragment_index] = data[1:]

            if len(self.record_fragments) == 5:
                self._complete_record()
            return

        if header == STM32_LOG_ERROR_FRAME:
            requested_sequence = int.from_bytes(data[4:8], "little")
            self.protocol_error_count += 1
            self.last_error = (
                f"STM32 log error command=0x{data[1]:02X} "
                f"code=0x{data[2]:02X} sequence={requested_sequence}"
            )
            self.pending_kind = None
            self.pending_sequence = None
            self.next_sequence = None
            self.info_required = True
            self.next_info_time = time.monotonic()
            self._notify()
            return

        self.protocol_error_count += 1
        self.last_error = f"Unknown 0x55A header=0x{header:02X}"
        self._notify()

    def _complete_info(self):
        wire_info = b"".join(
            self.info_fragments[index] for index in range(3)
        )[:18]

        version = wire_info[0]
        record_size = wire_info[1]
        count = int.from_bytes(wire_info[2:4], "little")
        capacity = int.from_bytes(wire_info[4:6], "little")
        oldest_sequence = int.from_bytes(wire_info[6:10], "little")
        latest_sequence = int.from_bytes(wire_info[10:14], "little")

        self.pending_kind = None
        self.info_fragments.clear()

        if (version != STM32_LOG_PROTOCOL_VERSION or
                record_size != STM32_LOG_RECORD_SIZE or
                count > capacity or
                (count > 0 and oldest_sequence > latest_sequence)):
            self.protocol_error_count += 1
            self.last_error = (
                f"Invalid info version={version} size={record_size} "
                f"count={count} capacity={capacity}"
            )
            self.info_required = True
            self.next_info_time = (
                time.monotonic() + STM32_LOG_INFO_RETRY_DELAY_S
            )
            self._notify()
            return

        if (self.last_saved_sequence != 0 and
                latest_sequence < self.last_saved_sequence):
            self.file_path = None
            self.saved_sequences.clear()
            self.record_retries.clear()
            self.last_saved_sequence = 0
            self.next_sequence = None

        self.latest_sequence = latest_sequence
        self.info_required = False
        self.bootstrap_complete = True

        if count == 0:
            self.next_sequence = None
        elif self.next_sequence is None:
            self.next_sequence = oldest_sequence
        elif self.next_sequence < oldest_sequence:
            self.missed_count += oldest_sequence - self.next_sequence
            self.next_sequence = oldest_sequence

        self.last_error = None
        self._notify()

    def _handle_heartbeat(self, data):
        version = data[0]
        flags = data[1]
        latest_sequence = int.from_bytes(data[2:6], "little")
        record_count = data[6]
        alive_counter = data[7]

        if (version != STM32_LOG_PROTOCOL_VERSION or
                flags & ~STM32_LOG_HEARTBEAT_FLAG_MASK or
                record_count > STM32_LOG_RAM_CAPACITY):
            self.protocol_error_count += 1
            self.last_error = (
                f"Invalid 0x55B version={version} flags=0x{flags:02X} "
                f"count={record_count}"
            )
            self._notify()
            return

        now = time.monotonic()
        previous_alive = self.last_heartbeat_alive_counter

        if previous_alive is not None:
            alive_delta = (alive_counter - previous_alive) & 0xFF
            if alive_delta > 1:
                self.heartbeat_missed_count += alive_delta - 1

        self.heartbeat_rx_count += 1
        self.last_heartbeat_time = now
        self.last_heartbeat_alive_counter = alive_counter
        self.heartbeat_latest_sequence = latest_sequence
        self.heartbeat_record_count = record_count
        self.heartbeat_ready = bool(
            flags & STM32_LOG_HEARTBEAT_FLAG_READY
        )
        self.heartbeat_overwrite_detected = bool(
            flags & STM32_LOG_HEARTBEAT_FLAG_OVERWRITE
        )
        self.heartbeat_timeout_active = False

        if not self.heartbeat_ready:
            self._notify()
            return

        if self.bootstrap_complete:
            if latest_sequence < self.latest_sequence:
                self.info_required = True
                self.next_info_time = now
                self.next_sequence = None
            elif latest_sequence > self.latest_sequence:
                oldest_available = (
                    latest_sequence - record_count + 1
                    if record_count > 0 else latest_sequence + 1
                )
                requested_sequence = self.next_sequence

                if requested_sequence is None:
                    requested_sequence = (
                        self.last_saved_sequence + 1
                        if self.last_saved_sequence > 0
                        else oldest_available
                    )

                if requested_sequence < oldest_available:
                    self.info_required = True
                    self.next_info_time = now
                else:
                    self.next_sequence = requested_sequence
                    self.latest_sequence = latest_sequence

        self._notify()

    def _update_heartbeat_timeout(self, now):
        reference_time = (
            self.last_heartbeat_time
            if self.last_heartbeat_time is not None
            else self.heartbeat_wait_started
        )

        if ((now - reference_time) >= STM32_LOG_HEARTBEAT_TIMEOUT_S and
                not self.heartbeat_timeout_active):
            self.heartbeat_timeout_active = True
            self.heartbeat_timeout_count += 1
            self._notify()

    def _complete_record(self):
        raw_record = b"".join(
            self.record_fragments[index] for index in range(5)
        )[:STM32_LOG_RECORD_SIZE]
        requested_sequence = self.pending_sequence
        self.pending_kind = None
        self.pending_sequence = None
        self.record_fragments.clear()

        try:
            values = struct.unpack("<IIIIHBBIIHH", raw_record)
        except struct.error as error:
            self._reject_record(
                requested_sequence,
                f"Record unpack failed: {error}",
                crc_error=False,
            )
            return

        record = {
            "magic": values[0],
            "sequence": values[1],
            "uptime_ms": values[2],
            "rtc_epoch_s": values[3],
            "event_code": values[4],
            "source": values[5],
            "severity": values[6],
            "data_0": values[7],
            "data_1": values[8],
            "crc16": values[9],
            "commit_marker": values[10],
        }
        calculated_crc = self.calculate_crc16(raw_record[:28])

        validations = (
            (record["magic"] == STM32_LOG_RECORD_MAGIC,
             f"Invalid magic 0x{record['magic']:08X}", False),
            (record["commit_marker"] == STM32_LOG_COMMIT_MARKER,
             f"Invalid commit 0x{record['commit_marker']:04X}", False),
            (record["crc16"] == calculated_crc,
             f"CRC expected=0x{record['crc16']:04X} "
             f"calculated=0x{calculated_crc:04X}", True),
            (record["sequence"] == requested_sequence,
             f"Sequence response={record['sequence']} "
             f"requested={requested_sequence}", False),
        )

        for valid, description, crc_error in validations:
            if not valid:
                self._reject_record(
                    requested_sequence, description, crc_error
                )
                return

        if record["sequence"] not in self.saved_sequences:
            if not self.write_record(record):
                return
            self.saved_sequences.add(record["sequence"])

        self.record_retries.pop(record["sequence"], None)
        self.last_saved_sequence = record["sequence"]
        self.next_sequence = record["sequence"] + 1
        self.last_error = None
        self._notify()

    def _reject_record(self, sequence, description, crc_error):
        if crc_error:
            self.crc_error_count += 1
        else:
            self.protocol_error_count += 1

        retry_count = self.record_retries.get(sequence, 0) + 1
        self.record_retries[sequence] = retry_count
        self.last_error = description

        if retry_count > STM32_LOG_MAX_RECORD_RETRIES:
            self.missed_count += 1
            self.next_sequence = sequence + 1
            self.record_retries.pop(sequence, None)

        self._notify()
