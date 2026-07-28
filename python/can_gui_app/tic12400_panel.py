"""Simple end-user TIC12400 open/closed switch display."""

from PySide6.QtCore import Qt
from PySide6.QtGui import QBrush, QColor
from PySide6.QtWidgets import (
    QAbstractItemView,
    QGroupBox,
    QHeaderView,
    QLabel,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
)


class Tic12400Panel:
    """Show only confirmed, debounced switch states."""

    def __init__(self):
        self.channel_group = self._build_switch_table()

    def _build_switch_table(self):
        group = QGroupBox("TIC12400 Switch Inputs")
        layout = QVBoxLayout(group)

        self.status_label = QLabel("Waiting for switch data…")
        self.status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.status_label)

        self.channel_table = QTableWidget(24, 2)
        self.channel_table.setHorizontalHeaderLabels(("Switch", "State"))
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

        self.channel_table.setCurrentCell(-1, -1)
        layout.addWidget(self.channel_table)
        return group

    @staticmethod
    def _state_appearance(state):
        if state == "CLOSED":
            return "CLOSED", "#2da44e", True
        if state == "OPEN":
            return "OPEN", "#8c959f", True
        if state == "NOT_FITTED":
            return "Not available", "#8c959f", False
        return "Unavailable", "#cf222e", True

    def render(self, device_status, channels, switch_state):
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
        elif device_status["received"] and not device_status["healthy"]:
            self.status_label.setText("Switch module unavailable")
            self.status_label.setStyleSheet(
                "color: #cf222e; font-weight: 700;"
            )
        else:
            self.status_label.setText("Waiting for switch data…")
            self.status_label.setStyleSheet("color: #d19a36;")

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
