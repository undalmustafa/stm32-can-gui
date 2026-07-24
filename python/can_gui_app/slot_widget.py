"""Reusable slot configuration widget for the STM32 CAN GUI."""

from PySide6.QtWidgets import (
    QComboBox,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
)


class SlotWidget(QGroupBox):
    def __init__(self, title, slot_no, parent=None):
        super().__init__(title, parent)

        self.slot_no = slot_no

        self.id_input = QLineEdit()
        self.id_input.setPlaceholderText("Örn: 0x123 veya 18FF50E5")

        self.id_type_combo = QComboBox()
        self.id_type_combo.addItems(["Standard", "Extended"])

        self.cycle_spin = QSpinBox()
        self.cycle_spin.setRange(1, 60000)
        self.cycle_spin.setValue(50)
        self.cycle_spin.setSuffix(" ms")

        self.counter_spin = QSpinBox()
        self.counter_spin.setRange(1, 2_000_000_000)
        self.counter_spin.setValue(100)

        self.set_button = QPushButton("Set / Start")

        layout = QVBoxLayout()

        row1 = QHBoxLayout()
        row1.addWidget(QLabel("CAN ID:"))
        row1.addWidget(self.id_input)

        row2 = QHBoxLayout()
        row2.addWidget(QLabel("ID Type:"))
        row2.addWidget(self.id_type_combo)

        row3 = QHBoxLayout()
        row3.addWidget(QLabel("Cycle Time:"))
        row3.addWidget(self.cycle_spin)

        row4 = QHBoxLayout()
        row4.addWidget(QLabel("Counter:"))
        row4.addWidget(self.counter_spin)

        layout.addLayout(row1)
        layout.addLayout(row2)
        layout.addLayout(row3)
        layout.addLayout(row4)
        layout.addWidget(self.set_button)

        self.setLayout(layout)

