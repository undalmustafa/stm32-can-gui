import csv
import importlib
import struct
import sys
import tempfile
import types
from pathlib import Path


class DummyWidget:
    def __init__(self, *args, **kwargs):
        pass

    def __getattr__(self, name):
        return lambda *args, **kwargs: None


class DummyLabel(DummyWidget):
    def __init__(self):
        self.text_value = ""

    def setText(self, value):
        self.text_value = value


class FakeMessage:
    def __init__(self, **kwargs):
        self.__dict__.update(kwargs)


class FakeBus:
    def __init__(self):
        self.sent = []

    def send(self, message):
        self.sent.append(message)


def install_import_stubs():
    can_module = types.ModuleType("can")
    can_module.Message = FakeMessage
    can_module.Bus = FakeBus

    interfaces_module = types.ModuleType("can.interfaces")
    pcan_module = types.ModuleType("can.interfaces.pcan")
    basic_module = types.ModuleType("can.interfaces.pcan.basic")
    basic_module.PCAN_ERROR_BUSLIGHT = 0x00004
    basic_module.PCAN_ERROR_BUSHEAVY = 0x00008
    basic_module.PCAN_ERROR_BUSPASSIVE = 0x40000
    basic_module.PCAN_ERROR_BUSOFF = 0x00010

    qtcore_module = types.ModuleType("PySide6.QtCore")
    qtcore_module.QTimer = DummyWidget
    qtwidgets_module = types.ModuleType("PySide6.QtWidgets")

    for name in (
        "QApplication", "QWidget", "QLabel", "QLineEdit", "QPushButton",
        "QVBoxLayout", "QHBoxLayout", "QGroupBox", "QComboBox", "QSpinBox",
        "QMessageBox", "QTabWidget", "QCheckBox", "QScrollArea",
        "QFileDialog"
    ):
        setattr(qtwidgets_module, name, DummyWidget)

    pyside_module = types.ModuleType("PySide6")
    sys.modules["can"] = can_module
    sys.modules["can.interfaces"] = interfaces_module
    sys.modules["can.interfaces.pcan"] = pcan_module
    sys.modules["can.interfaces.pcan.basic"] = basic_module
    sys.modules["PySide6"] = pyside_module
    sys.modules["PySide6.QtCore"] = qtcore_module
    sys.modules["PySide6.QtWidgets"] = qtwidgets_module


def load_gui_module():
    install_import_stubs()
    package_root = Path(__file__).resolve().parents[1]
    gui_directory = package_root / "upload"

    if not (gui_directory / "can_gui_app").is_dir():
        gui_directory = package_root

    sys.path.insert(0, str(gui_directory))

    try:
        module = importlib.import_module("can_gui_app.stm32_log_sync")
    finally:
        sys.path.remove(str(gui_directory))

    return module


def create_sync(gui_module, directory):
    bus = FakeBus()
    status_changes = []
    observed_records = []
    sync = gui_module.Stm32LogSync(
        bus_provider=lambda: bus,
        enabled_provider=lambda: True,
        directory_provider=lambda: Path(directory),
        command_sender=lambda data: (
            bus.sent.append(FakeMessage(data=bytes(data))) or True
        ),
        status_changed=lambda: status_changes.append(True),
        record_observer=lambda record: observed_records.append(record),
    )
    sync.reset()
    return sync, bus, status_changes, observed_records


def make_fragments(base, payload, fragment_count):
    fragments = []

    for index in range(fragment_count):
        chunk = payload[index * 7:(index + 1) * 7]
        chunk += bytes(7 - len(chunk))
        fragments.append(FakeMessage(data=bytes([base | index]) + chunk))

    return fragments


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    module = load_gui_module()

    with tempfile.TemporaryDirectory() as temp_directory:
        sync, bus, status_changes, observed_records = create_sync(
            module, temp_directory
        )

        sync.process()
        expect(len(bus.sent) == 1, "GUI sends log-info request")
        expect(bytes(bus.sent[0].data) == bytes([0x30, 0, 0, 0, 0, 0, 0, 0]),
               "log-info command payload")

        info_payload = bytearray(18)
        info_payload[0] = 1
        info_payload[1] = 32
        info_payload[2:4] = (1).to_bytes(2, "little")
        info_payload[4:6] = (64).to_bytes(2, "little")
        info_payload[6:10] = (1).to_bytes(4, "little")
        info_payload[10:14] = (1).to_bytes(4, "little")

        for fragment in make_fragments(0x70, bytes(info_payload), 3):
            sync.handle_message(fragment)

        expect(sync.next_sequence == 1,
               "info response selects oldest sequence")

        sync.process()
        expect(len(bus.sent) == 2, "GUI sends record request")
        expect(bytes(bus.sent[1].data) == bytes([0x31, 1, 0, 0, 0, 0, 0, 0]),
               "record command encodes sequence little-endian")

        record_prefix = struct.pack(
            "<IIIIHBBII",
            0x4C4F4731,
            1,
            123,
            0,
            0x0001,
            0,
            0,
            0,
            0,
        )
        crc16 = sync.calculate_crc16(record_prefix)
        record = record_prefix + struct.pack("<HH", crc16, 0xA55A)

        for fragment in make_fragments(0x80, record, 5):
            sync.handle_message(fragment)

        expect(sync.saved_count == 1,
               "one validated record is saved")
        expect(sync.crc_error_count == 0,
               "valid record has no CRC error")
        expect(sync.next_sequence == 2,
               "sync advances to next sequence")
        expect(len(status_changes) > 0,
               "service reports state changes to the UI")
        expect(len(observed_records) == 1,
               "validated records are published to the recent-event view")
        expect(observed_records[0]["event_name"] == "SYSTEM_BOOT",
               "published records include the decoded event name")

        log_files = list(Path(temp_directory).glob("stm32_events_*.csv"))
        expect(len(log_files) == 1, "separate STM32 CSV file is created")

        with log_files[0].open("r", encoding="utf-8-sig", newline="") as file:
            rows = list(csv.DictReader(file))

        expect(len(rows) == 1, "CSV contains one record")
        expect(rows[0]["sequence"] == "1", "CSV sequence value")
        expect(rows[0]["event_name"] == "SYSTEM_BOOT", "CSV event decoding")
        expect(rows[0]["commit_marker"] == "0xA55A", "CSV commit marker")

    print("PASS: GUI STM32 log sync, validation and separate CSV")


if __name__ == "__main__":
    main()
