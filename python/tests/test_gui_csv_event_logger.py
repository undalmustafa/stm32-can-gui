import csv
import sys
import tempfile
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
GUI_DIRECTORY = PACKAGE_ROOT / "upload"

if not (GUI_DIRECTORY / "can_gui_app").is_dir():
    GUI_DIRECTORY = PACKAGE_ROOT

sys.path.insert(0, str(GUI_DIRECTORY))

from can_gui_app.csv_event_logger import CsvEventLogger  # noqa: E402


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    with tempfile.TemporaryDirectory() as temp_directory:
        logger = CsvEventLogger(temp_directory)

        result = logger.write(
            source="COMMAND",
            severity="INFO",
            event_code="RTC_SET_TIME",
            rtc_time="17/07/2026 20:00:00.00",
            detail="=external formula",
            direction="TX",
            can_id=0x1894AABB,
            payload=bytes([0x20, 1, 2, 3]),
        )

        expect(result, "first CSV event is written")
        expect(logger.write_count == 1, "write counter increments")
        expect(logger.error_count == 0, "successful write has no error")
        expect(logger.last_path is not None, "active path is retained")

        with logger.last_path.open(
            "r", encoding="utf-8-sig", newline=""
        ) as file:
            rows = list(csv.DictReader(file))

        expect(len(rows) == 1, "CSV contains one row")
        expect(rows[0]["sequence"] == "1", "sequence starts at one")
        expect(rows[0]["can_id"] == "0x1894AABB", "CAN ID is hex")
        expect(rows[0]["dlc"] == "4", "DLC is preserved")
        expect(rows[0]["payload_hex"] == "20 01 02 03",
               "payload formatting is preserved")
        expect(rows[0]["detail"] == "'=external formula",
               "spreadsheet formula injection remains blocked")

        logger.enabled = False
        expect(not logger.write("SYSTEM", "INFO", "IGNORED"),
               "disabled logger rejects writes")
        expect(logger.write_count == 1,
               "disabled write does not change write counter")

        logger.enabled = True
        invalid_directory = Path(temp_directory) / "not_a_directory"
        invalid_directory.write_text("file", encoding="utf-8")
        logger.set_directory(invalid_directory)
        expect(not logger.write("SYSTEM", "FAULT", "WRITE_FAILURE"),
               "filesystem failure is reported")
        expect(logger.error_count == 1, "write error counter increments")
        expect(logger.last_error is not None, "last error is retained")

    print("PASS: GUI CSV event logger")


if __name__ == "__main__":
    main()
