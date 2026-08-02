"""Qt panel for signed A/B firmware updates over UDS."""

from PySide6.QtWidgets import (
    QFileDialog,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QProgressBar,
    QPushButton,
    QVBoxLayout,
)


class FlashPanel:
    def __init__(self, flash_requested, dialog_parent=None):
        self._flash_requested = flash_requested
        self._dialog_parent = dialog_parent
        self.artifact_group = self._build_artifact_group()
        self.progress_group = self._build_progress_group()

    def _build_artifact_group(self):
        group = QGroupBox("Signed firmware artifact")
        layout = QVBoxLayout(group)
        path_row = QHBoxLayout()
        self.path_edit = QLineEdit()
        self.path_edit.setReadOnly(True)
        self.path_edit.setPlaceholderText("Select a signed slot image (*.img)")
        self.browse_button = QPushButton("Browse…")
        self.browse_button.clicked.connect(self._browse)
        path_row.addWidget(self.path_edit)
        path_row.addWidget(self.browse_button)
        self.flash_button = QPushButton("Flash inactive slot")
        self.flash_button.setEnabled(False)
        self.flash_button.clicked.connect(self._start)
        help_label = QLabel(
            "The ECU selects the inactive A/B slot. The signed manifest is "
            "committed only after payload verification."
        )
        help_label.setWordWrap(True)
        layout.addLayout(path_row)
        layout.addWidget(help_label)
        layout.addWidget(self.flash_button)
        return group

    def _build_progress_group(self):
        group = QGroupBox("Firmware update")
        layout = QGridLayout(group)
        self.status_label = QLabel("IDLE")
        self.detail_label = QLabel("Select a signed image artifact")
        self.detail_label.setWordWrap(True)
        self.artifact_label = QLabel("-")
        self.target_label = QLabel("-")
        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        layout.addWidget(QLabel("State"), 0, 0)
        layout.addWidget(self.status_label, 0, 1)
        layout.addWidget(QLabel("Detail"), 1, 0)
        layout.addWidget(self.detail_label, 1, 1)
        layout.addWidget(QLabel("Artifact"), 2, 0)
        layout.addWidget(self.artifact_label, 2, 1)
        layout.addWidget(QLabel("Target"), 3, 0)
        layout.addWidget(self.target_label, 3, 1)
        layout.addWidget(self.progress_bar, 4, 0, 1, 2)
        return group

    def _browse(self):
        path, _filter = QFileDialog.getOpenFileName(
            self._dialog_parent,
            "Select signed firmware artifact",
            "",
            "Signed firmware image (*.img);;All files (*)",
        )
        if path:
            self.path_edit.setText(path)
            self.flash_button.setEnabled(True)

    def _start(self):
        path = self.path_edit.text().strip()
        if path:
            self._flash_requested(path)

    def render(self, status, detail, progress, busy, artifact, target):
        self.status_label.setText(status)
        self.detail_label.setText(detail)
        self.artifact_label.setText(artifact)
        self.target_label.setText(target)
        self.progress_bar.setValue(int(progress))
        self.path_edit.setEnabled(not busy)
        self.browse_button.setEnabled(not busy)
        self.flash_button.setEnabled(not busy and bool(
            self.path_edit.text().strip()
        ))
