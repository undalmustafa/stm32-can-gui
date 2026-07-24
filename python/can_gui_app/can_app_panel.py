"""Slot configuration, LED controls and application status widgets."""

from PySide6.QtWidgets import (
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
)

from .slot_widget import SlotWidget


class CanAppPanel:
    """Own Slot/LED widgets while leaving CAN protocol to the controller."""

    def __init__(self, slot_start_requested, led_command_requested):
        self._slot_start_requested = slot_start_requested
        self._led_command_requested = led_command_requested

        self.slot1 = SlotWidget("Slot 1", 1)
        self.slot1.id_input.setText("0x123")
        self.slot1.id_type_combo.setCurrentText("Standard")
        self.slot1.cycle_spin.setValue(50)
        self.slot1.counter_spin.setValue(100)

        self.slot2 = SlotWidget("Slot 2", 2)
        self.slot2.id_input.setText("0x18FF50E5")
        self.slot2.id_type_combo.setCurrentText("Extended")
        self.slot2.cycle_spin.setValue(50)
        self.slot2.counter_spin.setValue(200)

        self.led_control_group = self._build_led_control_group()
        (
            self.slot1_status_group,
            self.slot2_status_group,
            self.led_status_group,
        ) = self._build_status_groups()

        self.slot1.set_button.clicked.connect(
            lambda: self.request_slot_start(self.slot1)
        )
        self.slot2.set_button.clicked.connect(
            lambda: self.request_slot_start(self.slot2)
        )

    def _build_led_control_group(self):
        self.led1_on_button = QPushButton("LED1 ON")
        self.led1_off_button = QPushButton("LED1 OFF")
        self.led2_on_button = QPushButton("LED2 ON")
        self.led2_off_button = QPushButton("LED2 OFF")

        group = QGroupBox("LED Control")
        layout = QHBoxLayout()
        layout.addWidget(self.led1_on_button)
        layout.addWidget(self.led1_off_button)
        layout.addWidget(self.led2_on_button)
        layout.addWidget(self.led2_off_button)
        group.setLayout(layout)

        self.led1_on_button.clicked.connect(
            lambda: self._led_command_requested(1, 1)
        )
        self.led1_off_button.clicked.connect(
            lambda: self._led_command_requested(1, 0)
        )
        self.led2_on_button.clicked.connect(
            lambda: self._led_command_requested(2, 1)
        )
        self.led2_off_button.clicked.connect(
            lambda: self._led_command_requested(2, 0)
        )
        return group

    def _build_status_groups(self):
        self.slot1_status_label = QLabel()
        self.slot2_status_label = QLabel()
        self.led_status_label = QLabel()

        for label in (
            self.slot1_status_label,
            self.slot2_status_label,
            self.led_status_label,
        ):
            label.setStyleSheet("font-family: Consolas; font-size: 13px;")

        slot1_group = QGroupBox("Slot 1 Status")
        slot1_layout = QVBoxLayout()
        slot1_layout.addWidget(self.slot1_status_label)
        slot1_group.setLayout(slot1_layout)

        slot2_group = QGroupBox("Slot 2 Status")
        slot2_layout = QVBoxLayout()
        slot2_layout.addWidget(self.slot2_status_label)
        slot2_group.setLayout(slot2_layout)

        led_group = QGroupBox("LED Status")
        led_layout = QVBoxLayout()
        led_layout.addWidget(self.led_status_label)
        led_group.setLayout(led_layout)
        return slot1_group, slot2_group, led_group

    @staticmethod
    def get_slot_request(slot_widget):
        return {
            "slot_no": slot_widget.slot_no,
            "can_id_text": slot_widget.id_input.text(),
            "id_type_text": slot_widget.id_type_combo.currentText(),
            "cycle_time": slot_widget.cycle_spin.value(),
            "counter": slot_widget.counter_spin.value(),
        }

    def request_slot_start(self, slot_widget):
        self._slot_start_requested(**self.get_slot_request(slot_widget))

    def render_status(self, slot_status, led_status):
        slot1 = slot_status[1]
        slot2 = slot_status[2]

        self.slot1_status_label.setText(
            f"CAN ID     : {slot1['can_id']}\n"
            f"ID Type    : {slot1['id_type']}\n"
            f"Cycle Time : {slot1['cycle_time']}\n"
            f"Counter    : {slot1['counter']}\n"
            f"State      : {slot1['state']}"
        )
        self.slot2_status_label.setText(
            f"CAN ID     : {slot2['can_id']}\n"
            f"ID Type    : {slot2['id_type']}\n"
            f"Cycle Time : {slot2['cycle_time']}\n"
            f"Counter    : {slot2['counter']}\n"
            f"State      : {slot2['state']}"
        )
        self.led_status_label.setText(
            f"LED1 : {led_status[1]}\n"
            f"LED2 : {led_status[2]}"
        )
