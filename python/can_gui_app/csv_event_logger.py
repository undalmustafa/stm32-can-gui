"""Daily CSV event logger used by the desktop GUI."""

import csv
import time
from datetime import datetime
from pathlib import Path


EVENT_LOG_HEADERS = (
    "sequence",
    "host_time_iso",
    "gui_uptime_ms",
    "rtc_time",
    "direction",
    "source",
    "severity",
    "event_code",
    "can_id",
    "dlc",
    "payload_hex",
    "detail",
)


class CsvEventLogger:
    def __init__(self, directory, enabled=True):
        self.enabled = bool(enabled)
        self.directory = Path(directory)
        self.started_at = time.monotonic()
        self.sequence = 0
        self.write_count = 0
        self.error_count = 0
        self.last_path = None
        self.last_error = None

    def get_file_path(self):
        file_name = datetime.now().strftime("events_%Y%m%d.csv")
        return self.directory / file_name

    def set_directory(self, directory):
        self.directory = Path(directory)
        self.last_path = None
        self.last_error = None

    @staticmethod
    def clean_text(value):
        text = str(value or "").replace("\r", " ").replace("\n", " ")

        # Prevent spreadsheet applications from evaluating external text.
        if text.startswith(("=", "+", "-", "@")):
            text = "'" + text

        return text

    def write(self,
              source,
              severity,
              event_code,
              rtc_time="",
              detail="",
              direction="INTERNAL",
              can_id=None,
              payload=None):
        if not self.enabled:
            return False

        payload_bytes = bytes(payload) if payload is not None else b""
        payload_hex = " ".join(f"{byte:02X}" for byte in payload_bytes)

        if isinstance(can_id, int):
            can_id_text = f"0x{can_id:X}"
        else:
            can_id_text = str(can_id or "")

        self.sequence += 1
        log_path = self.get_file_path()
        row = {
            "sequence": self.sequence,
            "host_time_iso": datetime.now().astimezone().isoformat(
                timespec="milliseconds"
            ),
            "gui_uptime_ms": int(
                (time.monotonic() - self.started_at) * 1000
            ),
            "rtc_time": rtc_time,
            "direction": self.clean_text(direction),
            "source": self.clean_text(source),
            "severity": self.clean_text(severity),
            "event_code": self.clean_text(event_code),
            "can_id": self.clean_text(can_id_text),
            "dlc": len(payload_bytes) if payload is not None else "",
            "payload_hex": payload_hex,
            "detail": self.clean_text(detail),
        }

        try:
            self.directory.mkdir(parents=True, exist_ok=True)
            write_header = not log_path.exists() or log_path.stat().st_size == 0
            file_encoding = "utf-8-sig" if write_header else "utf-8"

            with log_path.open(
                "a",
                newline="",
                encoding=file_encoding
            ) as file:
                writer = csv.DictWriter(file, fieldnames=EVENT_LOG_HEADERS)

                if write_header:
                    writer.writeheader()

                writer.writerow(row)

            self.write_count += 1
            self.last_path = log_path
            self.last_error = None
            return True
        except (OSError, csv.Error) as error:
            self.error_count += 1
            self.last_error = f"{type(error).__name__}: {error}"
            return False

