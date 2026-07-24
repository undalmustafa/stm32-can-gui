"""RTC configuration and value widgets for the STM32 CAN GUI."""

from datetime import date

from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
)


WEEKDAY_ITEMS = [
    "Pazar (0)",
    "Pazartesi (1)",
    "Salı (2)",
    "Çarşamba (3)",
    "Perşembe (4)",
    "Cuma (5)",
    "Cumartesi (6)",
]


class RtcPanel:
    """Own RTC widgets while leaving protocol work to ``RtcController``."""

    def __init__(self, set_datetime_requested, set_alarm_requested,
                 disable_alarm_requested):
        self._set_datetime_requested = set_datetime_requested
        self._set_alarm_requested = set_alarm_requested
        self._disable_alarm_requested = disable_alarm_requested

        self.calendar_group = self._build_calendar_group()
        self.alarm_configuration_group = self._build_alarm_group()
        self.values_group = self._build_values_group()

        self.set_weekday_auto_mode(True)
        self.update_alarm_field_states()

    def _build_calendar_group(self):
        self.set_hundredth_spin = QSpinBox()
        self.set_hundredth_spin.setRange(0, 99)

        self.set_hour_spin = QSpinBox()
        self.set_hour_spin.setRange(0, 23)

        self.set_minute_spin = QSpinBox()
        self.set_minute_spin.setRange(0, 59)

        self.set_second_spin = QSpinBox()
        self.set_second_spin.setRange(0, 59)

        self.set_day_spin = QSpinBox()
        self.set_day_spin.setRange(1, 31)
        self.set_day_spin.setValue(1)

        self.set_month_spin = QSpinBox()
        self.set_month_spin.setRange(1, 12)
        self.set_month_spin.setValue(1)

        self.set_year_spin = QSpinBox()
        self.set_year_spin.setRange(2000, 2099)
        self.set_year_spin.setValue(2026)

        self.set_weekday_combo = QComboBox()
        self.set_weekday_combo.addItems(WEEKDAY_ITEMS)

        self.auto_weekday_checkbox = QCheckBox(
            "Haftanın gününü tarihten otomatik hesapla"
        )
        self.auto_weekday_checkbox.setChecked(True)
        self.set_button = QPushButton("RTC Tarih/Saatini Yaz")

        group = QGroupBox("RTC Calendar Register Write")
        layout = QVBoxLayout()

        time_row = QHBoxLayout()
        time_row.addWidget(QLabel("Saat:"))
        time_row.addWidget(self.set_hour_spin)
        time_row.addWidget(QLabel("Dakika:"))
        time_row.addWidget(self.set_minute_spin)
        time_row.addWidget(QLabel("Saniye:"))
        time_row.addWidget(self.set_second_spin)
        time_row.addWidget(QLabel("1/100 s:"))
        time_row.addWidget(self.set_hundredth_spin)

        date_row = QHBoxLayout()
        date_row.addWidget(QLabel("Gün:"))
        date_row.addWidget(self.set_day_spin)
        date_row.addWidget(QLabel("Ay:"))
        date_row.addWidget(self.set_month_spin)
        date_row.addWidget(QLabel("Yıl:"))
        date_row.addWidget(self.set_year_spin)

        weekday_row = QHBoxLayout()
        weekday_row.addWidget(QLabel("Haftanın Günü:"))
        weekday_row.addWidget(self.set_weekday_combo)
        weekday_row.addWidget(self.auto_weekday_checkbox)

        layout.addLayout(time_row)
        layout.addLayout(date_row)
        layout.addLayout(weekday_row)
        layout.addWidget(self.set_button)
        group.setLayout(layout)

        self.set_button.clicked.connect(self._set_datetime_requested)
        self.set_day_spin.valueChanged.connect(self.update_weekday_from_date)
        self.set_month_spin.valueChanged.connect(self.update_weekday_from_date)
        self.set_year_spin.valueChanged.connect(self.update_weekday_from_date)
        self.auto_weekday_checkbox.toggled.connect(
            self.set_weekday_auto_mode
        )
        return group

    def _build_alarm_group(self):
        self.alarm_hour_enable = QCheckBox("Saat")
        self.alarm_hour_enable.setChecked(True)
        self.alarm_hour_spin = QSpinBox()
        self.alarm_hour_spin.setRange(0, 23)

        self.alarm_minute_enable = QCheckBox("Dakika")
        self.alarm_minute_enable.setChecked(True)
        self.alarm_minute_spin = QSpinBox()
        self.alarm_minute_spin.setRange(0, 59)

        self.alarm_second_enable = QCheckBox("Saniye")
        self.alarm_second_enable.setChecked(True)
        self.alarm_second_spin = QSpinBox()
        self.alarm_second_spin.setRange(0, 59)

        self.alarm_day_enable = QCheckBox("Ayın Günü")
        self.alarm_day_spin = QSpinBox()
        self.alarm_day_spin.setRange(1, 31)

        self.alarm_weekday_enable = QCheckBox("Haftanın Günü")
        self.alarm_weekday_combo = QComboBox()
        self.alarm_weekday_combo.addItems(WEEKDAY_ITEMS)

        self.alarm_set_button = QPushButton("Alarmı Kur")
        self.alarm_disable_button = QPushButton("Alarmı Kapat")

        group = QGroupBox("RTC Alarm")
        layout = QVBoxLayout()
        layout.setContentsMargins(8, 6, 8, 6)
        layout.setSpacing(4)

        time_row = QHBoxLayout()
        time_row.addWidget(self.alarm_hour_enable)
        time_row.addWidget(self.alarm_hour_spin)
        time_row.addWidget(self.alarm_minute_enable)
        time_row.addWidget(self.alarm_minute_spin)
        time_row.addWidget(self.alarm_second_enable)
        time_row.addWidget(self.alarm_second_spin)

        calendar_row = QHBoxLayout()
        calendar_row.addWidget(self.alarm_day_enable)
        calendar_row.addWidget(self.alarm_day_spin)
        calendar_row.addWidget(self.alarm_weekday_enable)
        calendar_row.addWidget(self.alarm_weekday_combo)

        button_row = QHBoxLayout()
        button_row.addWidget(self.alarm_set_button)
        button_row.addWidget(self.alarm_disable_button)

        layout.addLayout(time_row)
        layout.addLayout(calendar_row)
        layout.addLayout(button_row)
        group.setLayout(layout)

        self.alarm_set_button.clicked.connect(self._set_alarm_requested)
        self.alarm_disable_button.clicked.connect(
            self._disable_alarm_requested
        )
        for checkbox in (
            self.alarm_hour_enable,
            self.alarm_minute_enable,
            self.alarm_second_enable,
            self.alarm_day_enable,
            self.alarm_weekday_enable,
        ):
            checkbox.toggled.connect(self.update_alarm_field_states)
        return group

    def _build_values_group(self):
        self.time_label = QLabel("--:--:--")
        self.time_label.setStyleSheet("font-size: 42px; font-weight: bold;")

        self.date_label = QLabel("--/--/----")
        self.date_label.setStyleSheet("font-size: 24px; font-weight: bold;")

        self.weekday_label = QLabel("Weekday: UNKNOWN")
        self.raw_label = QLabel("RX 0x556: --")
        self.raw_label.setStyleSheet("font-family: Consolas; color: #555555;")

        self.health_label = QLabel(
            "Waiting for RTC status"
        )
        self.health_label.setObjectName("statusSummary")
        self.health_label.setWordWrap(True)
        self.rtc_health_badge = QLabel("Waiting")

        health_row = QHBoxLayout()
        health_row.addWidget(self.rtc_health_badge)
        health_row.addWidget(self.health_label, 1)

        self.link_status_label = QLabel()
        self.clock_status_label = QLabel()
        self.calendar_status_label = QLabel()
        components_row = QHBoxLayout()
        components_row.addWidget(self.link_status_label, 1)
        components_row.addWidget(self.clock_status_label, 1)
        components_row.addWidget(self.calendar_status_label, 1)

        self.event_badge = QLabel("No event")
        self.event_label = QLabel("No RTC status event received yet")
        self.event_label.setWordWrap(True)
        event_row = QHBoxLayout()
        event_row.addWidget(self.event_badge)
        event_row.addWidget(self.event_label, 1)

        diagnostics_group = QGroupBox("RTC Health")
        diagnostics_layout = QVBoxLayout()
        diagnostics_layout.setContentsMargins(10, 8, 10, 10)
        diagnostics_layout.setSpacing(8)
        diagnostics_layout.addLayout(health_row)
        diagnostics_layout.addLayout(components_row)
        diagnostics_layout.addLayout(event_row)
        diagnostics_group.setLayout(diagnostics_layout)

        self._set_badge(self.rtc_health_badge, "Waiting", "UNKNOWN")
        self._set_component(
            self.link_status_label, "I2C connection", "Waiting", "UNKNOWN"
        )
        self._set_component(
            self.clock_status_label, "Clock integrity", "Waiting", "UNKNOWN"
        )
        self._set_component(
            self.calendar_status_label, "Calendar data", "Waiting", "UNKNOWN"
        )
        self._set_badge(self.event_badge, "No event", "UNKNOWN")

        self.alarm_badge = QLabel("Waiting")
        self.alarm_status_label = QLabel("Waiting for alarm status")
        self.alarm_status_label.setWordWrap(True)
        self._set_badge(self.alarm_badge, "Waiting", "UNKNOWN")
        alarm_status_group = QGroupBox("RTC Alarm Status")
        alarm_status_layout = QHBoxLayout()
        alarm_status_layout.setContentsMargins(10, 8, 10, 10)
        alarm_status_layout.addWidget(self.alarm_badge)
        alarm_status_layout.addWidget(self.alarm_status_label, 1)
        alarm_status_group.setLayout(alarm_status_layout)

        group = QGroupBox("PCA2131 RTC Values")
        layout = QVBoxLayout()
        layout.addWidget(QLabel("PCA2131 RTC Saati"))
        layout.addWidget(self.time_label)
        layout.addWidget(QLabel("PCA2131 RTC Tarihi"))
        layout.addWidget(self.date_label)
        layout.addWidget(self.weekday_label)
        layout.addWidget(self.raw_label)
        layout.addWidget(diagnostics_group)
        layout.addWidget(alarm_status_group)
        group.setLayout(layout)
        return group

    @staticmethod
    def _status_colors(state):
        return {
            "OK": ("#ECFDF3", "#166534"),
            "WARN": ("#FFFAEB", "#8A5A00"),
            "FAULT": ("#FEF3F2", "#B42318"),
            "UNKNOWN": ("#F2F4F7", "#475467"),
        }.get(state, ("#F2F4F7", "#475467"))

    @classmethod
    def _set_badge(cls, label, text, state):
        background, foreground = cls._status_colors(state)
        label.setText(text)
        label.setStyleSheet(
            f"background: {foreground}; color: white; border-radius: 4px; "
            "font-weight: 700; padding: 4px 8px;"
        )

    @classmethod
    def _set_component(cls, label, title, value, state):
        background, foreground = cls._status_colors(state)
        label.setText(f"{title}\n{value}")
        label.setStyleSheet(
            f"background: {background}; color: {foreground}; "
            "border-radius: 4px; padding: 7px; font-weight: 600;"
        )

    def get_datetime_request(self):
        return {
            "hundredth": self.set_hundredth_spin.value(),
            "second": self.set_second_spin.value(),
            "minute": self.set_minute_spin.value(),
            "hour": self.set_hour_spin.value(),
            "day": self.set_day_spin.value(),
            "month": self.set_month_spin.value(),
            "full_year": self.set_year_spin.value(),
            "auto_weekday": self.auto_weekday_checkbox.isChecked(),
            "weekday": self.set_weekday_combo.currentIndex(),
        }

    def get_alarm_request(self):
        return {
            "second_enabled": self.alarm_second_enable.isChecked(),
            "minute_enabled": self.alarm_minute_enable.isChecked(),
            "hour_enabled": self.alarm_hour_enable.isChecked(),
            "day_enabled": self.alarm_day_enable.isChecked(),
            "weekday_enabled": self.alarm_weekday_enable.isChecked(),
            "second": self.alarm_second_spin.value(),
            "minute": self.alarm_minute_spin.value(),
            "hour": self.alarm_hour_spin.value(),
            "day": self.alarm_day_spin.value(),
            "weekday": self.alarm_weekday_combo.currentIndex(),
        }

    def get_log_time(self):
        rtc_time = self.time_label.text().strip()
        rtc_date = self.date_label.text().strip()
        if "--" in rtc_time or "--" in rtc_date:
            return ""
        return f"{rtc_date} {rtc_time}"

    def set_datetime_weekday(self, weekday):
        self.set_weekday_combo.setCurrentIndex(weekday)

    def update_weekday_from_date(self, *_args):
        if not self.auto_weekday_checkbox.isChecked():
            return
        try:
            selected_date = date(
                self.set_year_spin.value(),
                self.set_month_spin.value(),
                self.set_day_spin.value(),
            )
        except ValueError:
            return
        self.set_weekday_combo.setCurrentIndex(
            (selected_date.weekday() + 1) % 7
        )

    def set_weekday_auto_mode(self, enabled):
        self.set_weekday_combo.setEnabled(not enabled)
        if enabled:
            self.update_weekday_from_date()

    def update_alarm_field_states(self, *_args):
        self.alarm_hour_spin.setEnabled(self.alarm_hour_enable.isChecked())
        self.alarm_minute_spin.setEnabled(
            self.alarm_minute_enable.isChecked()
        )
        self.alarm_second_spin.setEnabled(
            self.alarm_second_enable.isChecked()
        )
        self.alarm_day_spin.setEnabled(self.alarm_day_enable.isChecked())
        self.alarm_weekday_combo.setEnabled(
            self.alarm_weekday_enable.isChecked()
        )

    def render_diagnostics(self, health_text, health_color, link_text,
                           clock_text, calendar_state, event):
        health_state = health_text if health_text in {"OK", "FAULT"} else "UNKNOWN"
        health_summary = {
            "OK": "RTC is healthy and ready",
            "FAULT": "RTC needs attention",
            "UNKNOWN": "Waiting for complete RTC status",
        }[health_state]
        health_badge = {
            "OK": "Healthy",
            "FAULT": "Problem",
            "UNKNOWN": "Waiting",
        }[health_state]
        self.health_label.setText(health_summary)
        self.health_label.setStyleSheet(
            f"color: {health_color}; font-weight: 700;"
        )
        self._set_badge(self.rtc_health_badge, health_badge, health_state)

        link_state = "OK" if link_text.endswith("=OK") else (
            "FAULT" if link_text.endswith("=FAULT") else "UNKNOWN"
        )
        link_value = {
            "OK": "Connected",
            "FAULT": "Not responding",
            "UNKNOWN": "Waiting",
        }[link_state]
        if "INVALID" in clock_text:
            clock_state, clock_value = "FAULT", "Invalid (OSF set)"
        elif "VALID" in clock_text:
            clock_state, clock_value = "OK", "Valid"
        else:
            clock_state, clock_value = "UNKNOWN", "Waiting"
        calendar_map = {
            "VALID": ("OK", "Valid"),
            "INVALID": ("FAULT", "Invalid"),
            "STALE": ("WARN", "Last value is stale"),
        }
        calendar_status, calendar_value = calendar_map.get(
            calendar_state, ("UNKNOWN", "Waiting")
        )
        self._set_component(
            self.link_status_label, "I2C connection", link_value, link_state
        )
        self._set_component(
            self.clock_status_label,
            "Clock integrity",
            clock_value,
            clock_state,
        )
        self._set_component(
            self.calendar_status_label,
            "Calendar data",
            calendar_value,
            calendar_status,
        )
        technical_tooltip = (
            f"Health: {health_text}\n{link_text}\n{clock_text}\n"
            f"Calendar: {calendar_state}"
        )
        for label in (
            self.rtc_health_badge,
            self.health_label,
            self.link_status_label,
            self.clock_status_label,
            self.calendar_status_label,
        ):
            label.setToolTip(technical_tooltip)
        if event is None:
            return

        event_state = {
            "INFO": "OK",
            "WARN": "WARN",
            "FAULT": "FAULT",
        }.get(event["severity"], "UNKNOWN")
        event_badge = {
            "OK": "Information",
            "WARN": "Warning",
            "FAULT": "Error",
            "UNKNOWN": "Event",
        }[event_state]
        self._set_badge(self.event_badge, event_badge, event_state)
        self.event_label.setText(event["description"])
        self.event_label.setToolTip(
            f"Code: {event['code']} {event['mnemonic']}\n"
            f"HAL: {event['hal']}\nI2C: {event['i2c']}\n"
            f"Error mask: {event['error_mask']}"
        )

    def render_time(self, time_text, date_text, weekday_text, raw_text):
        self.time_label.setText(time_text)
        self.date_label.setText(date_text)
        self.weekday_label.setText(weekday_text)
        self.raw_label.setText(raw_text)

    def render_alarm(self, state, detail, color, tooltip=None):
        visual_state = {
            "ARMED": "OK",
            "DISABLED": "UNKNOWN",
            "PENDING": "WARN",
            "TRIGGERED": "WARN",
            "WARN": "WARN",
            "FAULT": "FAULT",
        }.get(state, "UNKNOWN")
        badge_text = {
            "ARMED": "Armed",
            "DISABLED": "Disabled",
            "PENDING": "Updating",
            "TRIGGERED": "Triggered",
            "WARN": "Warning",
            "FAULT": "Problem",
        }.get(state, "Waiting")
        self._set_badge(self.alarm_badge, badge_text, visual_state)
        self.alarm_status_label.setText(detail)
        self.alarm_status_label.setStyleSheet(
            f"color: {color}; font-weight: 600;"
        )
        if tooltip is not None:
            self.alarm_status_label.setToolTip(tooltip)
