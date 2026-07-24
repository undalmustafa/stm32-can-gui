"""PCA2131 RTC CAN protocol, state and command controller."""

from datetime import date

from .protocol import (
    CMD_RTC_SET_ALARM,
    CMD_RTC_SET_DATETIME,
    RTC_ALARM_ENABLE_DAY,
    RTC_ALARM_ENABLE_HOUR,
    RTC_ALARM_ENABLE_MINUTE,
    RTC_ALARM_ENABLE_SECOND,
    RTC_ALARM_ENABLE_WEEKDAY,
    RTC_ALARM_EVENT_RX_ID,
    RTC_COMMUNICATION_FAULT_CODES,
    RTC_STATUS_DEFINITIONS,
    RTC_STATUS_RX_ID,
    RTC_TIME_RX_ID,
    decode_hal_status,
    decode_i2c_error,
)


WEEKDAY_NAMES = {
    0: "Pazar",
    1: "Pazartesi",
    2: "Salı",
    3: "Çarşamba",
    4: "Perşembe",
    5: "Cuma",
    6: "Cumartesi",
}


class RtcController:
    def __init__(self,
                 command_sender,
                 event_writer,
                 diagnostics_renderer,
                 time_renderer,
                 alarm_renderer):
        self._command_sender = command_sender
        self._event_writer = event_writer
        self._diagnostics_renderer = diagnostics_renderer
        self._time_renderer = time_renderer
        self._alarm_renderer = alarm_renderer

        self.link_ok = None
        self.osf = None
        self.calendar_state = "UNKNOWN"
        self.last_event = None
        self.alarm_state = "UNKNOWN"
        self.alarm_pending_action = None

    def handle_message(self, msg):
        if msg.arbitration_id == RTC_STATUS_RX_ID:
            self.handle_status_message(msg)
            return True

        if msg.arbitration_id == RTC_TIME_RX_ID:
            self.handle_time_message(msg)
            return True

        if msg.arbitration_id == RTC_ALARM_EVENT_RX_ID:
            self.handle_alarm_event_message(msg)
            return True

        return False

    def handle_status_message(self, msg):
        data = list(msg.data)

        if len(data) < 6:
            return

        status_code = data[0]
        hal_status = data[1]
        hal_error = int.from_bytes(
            bytes(data[2:6]), byteorder="little", signed=False
        )
        severity, mnemonic, description = RTC_STATUS_DEFINITIONS.get(
            status_code,
            (
                "WARN",
                "UNKNOWN_STATUS",
                f"Unrecognized RTC status code 0x{status_code:02X}",
            ),
        )

        self.last_event = {
            "severity": severity,
            "code": f"0x{status_code:02X}",
            "mnemonic": mnemonic,
            "hal": decode_hal_status(hal_status),
            "i2c": decode_i2c_error(hal_error),
            "error_mask": f"0x{hal_error:08X}",
            "description": description,
        }

        self._event_writer(
            source="RTC_STATUS",
            severity=severity,
            event_code=f"0x{status_code:02X}_{mnemonic}",
            detail=(
                f"{description} | HAL={decode_hal_status(hal_status)} | "
                f"I2C={decode_i2c_error(hal_error)} | "
                f"ERROR_MASK=0x{hal_error:08X}"
            ),
            direction="RX",
            can_id=RTC_STATUS_RX_ID,
            payload=msg.data,
        )

        if status_code in RTC_COMMUNICATION_FAULT_CODES:
            self.link_ok = False
            self.calendar_state = "STALE"
        elif status_code in {0xA1, 0xA2, 0xA3, 0xA4}:
            self.link_ok = True

        if status_code == 0xA4:
            if self.alarm_pending_action == "DISABLE":
                self.set_alarm_status(
                    "DISABLED",
                    "All alarm comparisons are disabled",
                    "#555555",
                )
            else:
                self.set_alarm_status(
                    "ARMED",
                    "Register write and readback verification successful",
                    "#168018",
                )

            self.alarm_pending_action = None

        elif status_code in {0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1}:
            alarm_severity = (
                "WARN" if status_code in {0xEC, 0xED} else "FAULT"
            )
            alarm_color = (
                "#8A6D00" if alarm_severity == "WARN" else "#C62828"
            )
            self.set_alarm_status(
                alarm_severity,
                f"0x{status_code:02X} {mnemonic}: {description}",
                alarm_color,
            )
            self.alarm_pending_action = None

        self.render_diagnostics()

    def render_diagnostics(self):
        if self.link_ok is True:
            link_text = "I2C=OK"
        elif self.link_ok is False:
            link_text = "I2C=FAULT"
        else:
            link_text = "I2C=UNKNOWN"

        if self.osf is False:
            clock_text = "CLOCK=VALID(OSF=0)"
        elif self.osf is True:
            clock_text = "CLOCK=INVALID(OSF=1)"
        else:
            clock_text = "CLOCK=UNKNOWN"

        fault_present = (
            self.link_ok is False
            or self.osf is True
            or self.calendar_state in {"INVALID", "STALE"}
        )
        fully_operational = (
            self.link_ok is True
            and self.osf is False
            and self.calendar_state == "VALID"
        )

        if fault_present:
            health_text = "FAULT"
            health_color = "#C62828"
        elif fully_operational:
            health_text = "OK"
            health_color = "#168018"
        else:
            health_text = "UNKNOWN"
            health_color = "#8A6D00"

        self._diagnostics_renderer(
            health_text=health_text,
            health_color=health_color,
            link_text=link_text,
            clock_text=clock_text,
            calendar_state=self.calendar_state,
            event=self.last_event,
        )

    def handle_time_message(self, msg):
        data = list(msg.data)

        if len(data) != 8:
            return

        hour, minute, second, hundredth = data[0:4]
        day, month, year = data[4:7]
        flags = data[7]
        weekday = flags & 0x07
        calendar_valid = (flags & 0x20) != 0
        ready = (flags & 0x40) != 0
        osf = (flags & 0x80) != 0
        weekday_text = WEEKDAY_NAMES.get(weekday, f"INVALID ({weekday})")
        full_year = 2000 + year

        self._time_renderer(
            time_text=(
                f"{hour:02d}:{minute:02d}:{second:02d}.{hundredth:02d}"
            ),
            date_text=f"{day:02d}/{month:02d}/{full_year:04d}",
            weekday_text=f"Weekday: {weekday_text} ({weekday})",
            raw_text="RX 0x556: " + " ".join(
                f"{byte:02X}" for byte in data
            ),
        )

        self.link_ok = ready
        self.osf = osf
        self.calendar_state = "VALID" if calendar_valid else "INVALID"
        self.render_diagnostics()

    def handle_alarm_event_message(self, msg):
        data = list(msg.data)

        if len(data) != 8:
            self._event_writer(
                source="RTC_ALARM",
                severity="FAULT",
                event_code="INVALID_DLC",
                detail=f"Alarm event DLC={len(data)}; expected 8",
                direction="RX",
                can_id=RTC_ALARM_EVENT_RX_ID,
                payload=msg.data,
            )
            self.set_alarm_status(
                "FAULT",
                f"Invalid 0x558 DLC: {len(data)} (expected 8)",
                "#C62828",
            )
            return

        event_code = data[0]
        flags = data[1]
        alarm_flag = (flags & 0x01) != 0
        interrupt_enabled = (flags & 0x02) != 0
        configuration_valid = (flags & 0x04) != 0
        hour, minute, second = data[2:5]
        day, month = data[5:7]
        full_year = 2000 + data[7]

        if event_code != 0x01:
            self._event_writer(
                source="RTC_ALARM",
                severity="WARN",
                event_code=f"UNKNOWN_EVENT_0x{event_code:02X}",
                detail="Unknown RTC alarm event code",
                direction="RX",
                can_id=RTC_ALARM_EVENT_RX_ID,
                payload=msg.data,
            )
            self.set_alarm_status(
                "WARN",
                f"Unknown 0x558 event code 0x{event_code:02X}",
                "#8A6D00",
            )
            return

        self.alarm_pending_action = None
        detail = (
            f"{day:02d}/{month:02d}/{full_year:04d} "
            f"{hour:02d}:{minute:02d}:{second:02d} | "
            f"AF={int(alarm_flag)} AIE={int(interrupt_enabled)} "
            f"CONFIG={'VALID' if configuration_valid else 'INVALID'}"
        )
        self.set_alarm_status(
            "TRIGGERED",
            detail,
            "#C06000",
            tooltip="RX 0x558: " + " ".join(
                f"{byte:02X}" for byte in data
            ),
        )

        self._event_writer(
            source="RTC_ALARM",
            severity="INFO",
            event_code="TRIGGERED",
            detail=(
                f"RTC={day:02d}/{month:02d}/{full_year:04d} "
                f"{hour:02d}:{minute:02d}:{second:02d} | "
                f"AF={int(alarm_flag)} AIE={int(interrupt_enabled)} "
                f"CONFIG_VALID={int(configuration_valid)}"
            ),
            direction="RX",
            can_id=RTC_ALARM_EVENT_RX_ID,
            payload=msg.data,
        )

    def set_alarm_status(self, state, detail, color, tooltip=None):
        self.alarm_state = state
        self._alarm_renderer(
            state=state, detail=detail, color=color, tooltip=tooltip
        )

    def send_alarm(self,
                   second_enabled,
                   minute_enabled,
                   hour_enabled,
                   day_enabled,
                   weekday_enabled,
                   second,
                   minute,
                   hour,
                   day,
                   weekday):
        enable_mask = 0

        if second_enabled:
            enable_mask |= RTC_ALARM_ENABLE_SECOND
        if minute_enabled:
            enable_mask |= RTC_ALARM_ENABLE_MINUTE
        if hour_enabled:
            enable_mask |= RTC_ALARM_ENABLE_HOUR
        if day_enabled:
            enable_mask |= RTC_ALARM_ENABLE_DAY
        if weekday_enabled:
            enable_mask |= RTC_ALARM_ENABLE_WEEKDAY

        if enable_mask == 0:
            return "NO_FIELDS"

        data = [
            CMD_RTC_SET_ALARM,
            enable_mask,
            second if second_enabled else 0,
            minute if minute_enabled else 0,
            hour if hour_enabled else 0,
            day if day_enabled else 0,
            weekday if weekday_enabled else 0,
            0x00,
        ]

        if not self._command_sender(data):
            return "NOT_SENT"

        enabled_names = []
        if hour_enabled:
            enabled_names.append(f"H={hour:02d}")
        if minute_enabled:
            enabled_names.append(f"M={minute:02d}")
        if second_enabled:
            enabled_names.append(f"S={second:02d}")
        if day_enabled:
            enabled_names.append(f"DAY={day}")
        if weekday_enabled:
            enabled_names.append(f"WEEKDAY={weekday}")

        self.alarm_pending_action = "SET"
        self.set_alarm_status(
            "PENDING",
            "Write/readback verification: " + ", ".join(enabled_names),
            "#8A6D00",
        )
        return "SENT"

    def disable_alarm(self):
        data = [CMD_RTC_SET_ALARM, 0, 0, 0, 0, 0, 0, 0]

        if not self._command_sender(data):
            return False

        self.alarm_pending_action = "DISABLE"
        self.set_alarm_status(
            "PENDING", "Disabling all alarm comparisons", "#8A6D00"
        )
        return True

    def send_datetime(self,
                      hundredth,
                      second,
                      minute,
                      hour,
                      day,
                      month,
                      full_year,
                      auto_weekday,
                      weekday):
        selected_date = date(full_year, month, day)

        if auto_weekday:
            weekday = (selected_date.weekday() + 1) % 7

        month_weekday = (month & 0x1F) | ((weekday & 0x07) << 5)
        data = [
            CMD_RTC_SET_DATETIME,
            hundredth,
            second,
            minute,
            hour,
            day,
            month_weekday,
            full_year - 2000,
        ]
        sent = self._command_sender(data)

        if sent:
            self.last_event = {
                "severity": "INFO",
                "code": "TX 0x21",
                "mnemonic": "RTC_SET_DATETIME",
                "hal": "RESULT_PENDING",
                "i2c": "--",
                "error_mask": "--",
                "description": (
                    f"Requested {day:02d}/{month:02d}/{full_year:04d} "
                    f"{hour:02d}:{minute:02d}:{second:02d}."
                    f"{hundredth:02d}, weekday={weekday}"
                ),
            }
            self.render_diagnostics()

        return sent, weekday
