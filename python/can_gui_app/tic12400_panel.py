"""Simple end-user TIC12400 open/closed switch display."""

from PySide6.QtCore import Qt
from PySide6.QtGui import QBrush, QColor
from PySide6.QtWidgets import (
    QAbstractItemView,
    QComboBox,
    QGroupBox,
    QHeaderView,
    QLabel,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
)


class Tic12400Panel:
    """Show switch state and configure supported input polarity."""

    def __init__(self, polarity_requested=None):
        self._polarity_requested = polarity_requested
        self._polarity_combos = {}
        self._polarity_dirty = False
        self._pending_battery_mask = None
        self._syncing_polarity = False
        self.channel_group = self._build_switch_table()

    def _build_switch_table(self):
        group = QGroupBox("TIC12400 Switch Inputs")
        layout = QVBoxLayout(group)

        self.status_label = QLabel("Waiting for switch data…")
        self.status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.status_label)

        polarity_help = QLabel(
            "Polarity must match the wiring. Use − Ground for a switch "
            "to GND and + Battery only for a switch to VS. "
            "Press Nucleo B1 before applying."
        )
        polarity_help.setWordWrap(True)
        layout.addWidget(polarity_help)

        self.channel_table = QTableWidget(24, 3)
        self.channel_table.setHorizontalHeaderLabels(
            ("Switch", "State", "Polarity")
        )
        self.channel_table.setEditTriggers(
            QAbstractItemView.EditTrigger.NoEditTriggers
        )
        self.channel_table.setSelectionMode(
            QAbstractItemView.SelectionMode.NoSelection
        )
        self.channel_table.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.channel_table.setAlternatingRowColors(True)
        self.channel_table.verticalHeader().setVisible(False)
        self.channel_table.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.Stretch
        )

        for channel in range(24):
            input_item = QTableWidgetItem(f"IN{channel}")
            state_item = QTableWidgetItem(
                "Not available" if channel == 12 else "Waiting"
            )
            input_item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
            state_item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
            self.channel_table.setItem(channel, 0, input_item)
            self.channel_table.setItem(channel, 1, state_item)

            if channel < 10:
                polarity_combo = QComboBox()
                polarity_combo.addItem("− Ground", "GROUND")
                polarity_combo.addItem("+ Battery", "BATTERY")
                polarity_combo.currentIndexChanged.connect(
                    self._mark_polarity_dirty
                )
                self._polarity_combos[channel] = polarity_combo
                self.channel_table.setCellWidget(
                    channel, 2, polarity_combo
                )
            else:
                polarity_text = (
                    "Not available"
                    if channel == 12
                    else "− Ground (fixed)"
                )
                polarity_item = QTableWidgetItem(polarity_text)
                polarity_item.setTextAlignment(
                    Qt.AlignmentFlag.AlignCenter
                )
                self.channel_table.setItem(channel, 2, polarity_item)

        self.channel_table.setCurrentCell(-1, -1)
        layout.addWidget(self.channel_table)

        self.apply_polarity_button = QPushButton(
            "Apply switch polarity"
        )
        self.apply_polarity_button.setEnabled(False)
        self.apply_polarity_button.clicked.connect(
            self._apply_polarity
        )
        layout.addWidget(self.apply_polarity_button)

        self.polarity_status_label = QLabel(
            "Waiting for the applied polarity profile…"
        )
        self.polarity_status_label.setAlignment(
            Qt.AlignmentFlag.AlignCenter
        )
        layout.addWidget(self.polarity_status_label)
        return group

    def _mark_polarity_dirty(self, _index=None):
        if self._syncing_polarity:
            return
        self._polarity_dirty = True
        self.polarity_status_label.setText(
            "Polarity changed locally — press Apply"
        )

    def _selected_battery_mask(self):
        battery_mask = 0
        for channel, combo in self._polarity_combos.items():
            if combo.currentData() == "BATTERY":
                battery_mask |= 1 << channel
        return battery_mask

    def _apply_polarity(self):
        if self._polarity_requested is None:
            return

        battery_mask = self._selected_battery_mask()
        if self._polarity_requested(battery_mask):
            self._pending_battery_mask = battery_mask
            self.polarity_status_label.setText(
                "Polarity requested — awaiting MCU confirmation"
            )
        else:
            self.polarity_status_label.setText(
                "Polarity command was not sent"
            )

    def _sync_polarity_controls(self, battery_mask):
        self._syncing_polarity = True
        try:
            for channel, combo in self._polarity_combos.items():
                combo.setCurrentIndex(
                    1 if battery_mask & (1 << channel) else 0
                )
        finally:
            self._syncing_polarity = False

    @staticmethod
    def _state_appearance(state):
        if state == "CLOSED":
            return "CLOSED", "#2da44e", True
        if state == "OPEN":
            return "OPEN", "#8c959f", True
        if state == "NOT_FITTED":
            return "Not available", "#8c959f", False
        return "Unavailable", "#cf222e", True

    def render(self, device_status, channels, switch_state, profile):
        module_ready = (
            switch_state["received"]
            and switch_state["data_valid"]
            and (
                not device_status["received"]
                or device_status["healthy"]
            )
        )
        if module_ready:
            self.status_label.setText("Switch monitoring active")
            self.status_label.setStyleSheet(
                "color: #2da44e; font-weight: 700;"
            )
        elif (
            device_status["received"]
            and not device_status["online"]
        ):
            self.status_label.setText(
                "Switch module unavailable — retrying"
            )
            self.status_label.setStyleSheet(
                "color: #cf222e; font-weight: 700;"
            )
        elif device_status["received"] and not device_status["healthy"]:
            self.status_label.setText("Switch module fault")
            self.status_label.setStyleSheet(
                "color: #cf222e; font-weight: 700;"
            )
        else:
            self.status_label.setText("Waiting for switch data…")
            self.status_label.setStyleSheet("color: #d19a36;")

        profile_ready = (
            profile["received"]
            and profile["configuration_valid"]
        )
        self.apply_polarity_button.setEnabled(
            profile_ready and self._polarity_requested is not None
        )
        if profile_ready:
            confirmed_mask = profile["battery_switch_mask"]
            if (self._pending_battery_mask is not None and
                    self._pending_battery_mask == confirmed_mask):
                self._pending_battery_mask = None
                self._polarity_dirty = False
                self._sync_polarity_controls(confirmed_mask)
                self.polarity_status_label.setText(
                    "Polarity profile applied"
                )
            elif not self._polarity_dirty:
                self._sync_polarity_controls(confirmed_mask)
                self.polarity_status_label.setText(
                    "Polarity profile confirmed"
                )
        elif not self._polarity_dirty:
            self.polarity_status_label.setText(
                "Applied polarity profile unavailable"
            )

        for channel in channels:
            state = channel["state"]
            if channel["fitted"] and not module_ready:
                state = "UNAVAILABLE"
            text, color, bold = self._state_appearance(state)
            item = self.channel_table.item(channel["channel"], 1)
            item.setText(text)
            item.setForeground(QBrush(QColor(color)))
            font = item.font()
            font.setBold(bold)
            item.setFont(font)
            item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
