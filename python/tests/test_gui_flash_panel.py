import sys
import types
from pathlib import Path


class FakeSignal:
    def __init__(self):
        self.callback = None

    def connect(self, callback):
        self.callback = callback


class FakeWidget:
    def __init__(self, text=""):
        self._text = str(text)
        self.layout = None
        self.enabled = True
        self.word_wrap = False

    def setText(self, text):
        self._text = str(text)

    def text(self):
        return self._text

    def setEnabled(self, enabled):
        self.enabled = bool(enabled)

    def setWordWrap(self, enabled):
        self.word_wrap = bool(enabled)


class FakeLineEdit(FakeWidget):
    def setPlaceholderText(self, text):
        self.placeholder = text

    def setReadOnly(self, read_only):
        self.read_only = bool(read_only)


class FakeButton(FakeWidget):
    def __init__(self, text=""):
        super().__init__(text)
        self.clicked = FakeSignal()

    def click(self):
        self.clicked.callback()


class FakeProgress(FakeWidget):
    def setRange(self, minimum, maximum):
        self.range = (minimum, maximum)

    def setValue(self, value):
        self.value = value


class FakeLayout:
    def __init__(self, parent=None):
        self.items = []
        if parent is not None:
            parent.layout = self

    def addWidget(self, widget, *_args):
        self.items.append(widget)

    def addLayout(self, layout, *_args):
        self.items.append(layout)


class FakeFileDialog:
    selected_path = ""

    @classmethod
    def getOpenFileName(cls, *_args):
        return cls.selected_path, "image"


qtwidgets = types.ModuleType("PySide6.QtWidgets")
qtwidgets.QFileDialog = FakeFileDialog
qtwidgets.QGridLayout = FakeLayout
qtwidgets.QGroupBox = FakeWidget
qtwidgets.QHBoxLayout = FakeLayout
qtwidgets.QLabel = FakeWidget
qtwidgets.QLineEdit = FakeLineEdit
qtwidgets.QProgressBar = FakeProgress
qtwidgets.QPushButton = FakeButton
qtwidgets.QVBoxLayout = FakeLayout
pyside = types.ModuleType("PySide6")
pyside.QtWidgets = qtwidgets
sys.modules.setdefault("PySide6", pyside)
sys.modules.setdefault("PySide6.QtWidgets", qtwidgets)

PACKAGE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE_ROOT))

from can_gui_app.flash_panel import FlashPanel  # noqa: E402


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    requests = []
    panel = FlashPanel(requests.append)
    expect(not panel.flash_button.enabled,
           "flash action starts disabled without an artifact")

    FakeFileDialog.selected_path = "/tmp/release-slot-b.img"
    panel.browse_button.click()
    expect(panel.path_edit.text() == FakeFileDialog.selected_path,
           "artifact browser populates the selected path")
    panel.flash_button.click()
    expect(requests == [FakeFileDialog.selected_path],
           "flash action forwards the selected artifact")

    panel.render(
        status="TRANSFERRING",
        detail="Transferred 512/1040 bytes",
        progress=51,
        busy=True,
        artifact="release-slot-b.img | 1040 bytes",
        target="Slot B @ 0x08100000",
    )
    expect(panel.status_label.text() == "TRANSFERRING" and
           panel.progress_bar.value == 51,
           "workflow state and progress are visible")
    expect(not panel.path_edit.enabled and not panel.flash_button.enabled,
           "artifact controls are locked during a transfer")
    expect("Slot B" in panel.target_label.text(),
           "ECU-selected inactive slot is visible")

    print("PASS: signed firmware Flash panel controls and rendering")


if __name__ == "__main__":
    main()
