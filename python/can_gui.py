import sys
import logging
from pathlib import Path

from can_gui_app.application_timers import ApplicationTimers
from can_gui_app.can_app_controller import (
    CanAppController,
    SlotConfigurationError,
)
from can_gui_app.can_app_panel import CanAppPanel
from can_gui_app.can_connection_panel import CanConnectionPanel
from can_gui_app.can_health import CanHealthMonitor
from can_gui_app.can_session import CanSession
from can_gui_app.event_log_panel import EventLogPanel
from can_gui_app.main_window_view import MainWindowView
from can_gui_app.protocol import (
    STM32_LOG_HEARTBEAT_RX_ID,
    STM32_LOG_RESPONSE_RX_ID,
)
from can_gui_app.rtc_controller import RtcController
from can_gui_app.rtc_panel import RtcPanel
from can_gui_app.pwm_panel import PwmPanel
from can_gui_app.theme import apply_application_theme

from PySide6.QtWidgets import QApplication, QWidget, QMessageBox

logging.getLogger("can").setLevel(logging.ERROR)


class CanGui(QWidget):
    def __init__(self):
        super().__init__()

        self.rtc_panel = RtcPanel(
            set_datetime_requested=self.send_rtc_set_time,
            set_alarm_requested=self.send_rtc_alarm,
            disable_alarm_requested=self.disable_rtc_alarm,
        )

        self.can_app_panel = CanAppPanel(
            slot_start_requested=self.configure_and_start_slot,
            led_command_requested=self.send_led_command,
        )
        self.pwm_panel = PwmPanel(
            command_requested=self.send_pwm_command,
            self_test_requested=self.send_pwm_self_test,
        )

        self.can_connection_panel = CanConnectionPanel(
            connect_requested=self.connect_can,
        )

        self.can_app_controller = CanAppController(
            command_sender=self.send_can_command,
            status_renderer=self.can_app_panel.render_status,
            pwm_status_renderer=self.pwm_panel.render_status,
        )

        self.rtc_controller = RtcController(
            command_sender=self.send_can_command,
            event_writer=self.write_event_log,
            diagnostics_renderer=self.rtc_panel.render_diagnostics,
            time_renderer=self.rtc_panel.render_time,
            alarm_renderer=self.rtc_panel.render_alarm,
        )

        self.can_session = CanSession(
            event_writer=self.write_event_log,
            frame_handler=self.handle_application_message,
            error_frame_handler=(
                lambda: self.can_health.observe_error_frame()
            ),
            health_reporter=self.set_can_health,
        )

        self.event_log_panel = EventLogPanel(
            default_directory=Path(__file__).resolve().parent / "logs",
            bus_provider=lambda: self.can_session.bus,
            rtc_time_provider=self.rtc_panel.get_log_time,
            command_sender=self.send_can_command,
            dialog_parent=self,
        )

        self.can_health = CanHealthMonitor(
            bus_provider=lambda: self.can_session.bus,
            metrics_provider=self.can_session.get_health_metrics,
            event_writer=self.write_event_log,
            view_renderer=self.can_connection_panel.render_health,
        )

        self.setWindowTitle("STM32 CAN GUI")

        self.window_view = MainWindowView(
            can_connection_panel=self.can_connection_panel,
            event_log_panel=self.event_log_panel,
            can_app_panel=self.can_app_panel,
            rtc_panel=self.rtc_panel,
            pwm_panel=self.pwm_panel,
        )
        self.setLayout(self.window_view.root_layout)

        self.can_app_controller.render_status()
        self.event_log_panel.update_event_status()
        self.event_log_panel.update_stm32_status()

        self.application_timers = ApplicationTimers(
            can_rx_poll=self.can_session.poll,
            can_health_poll=self.can_health.monitor,
            stm32_log_sync=self.event_log_panel.process,
        )

        self.write_event_log(
            source="APPLICATION",
            severity="INFO",
            event_code="START",
            detail="STM32 CAN GUI started"
        )

    def write_event_log(self,
                        source,
                        severity,
                        event_code,
                        detail="",
                        direction="INTERNAL",
                        can_id=None,
                        payload=None):
        return self.event_log_panel.write_event(
            source=source,
            severity=severity,
            event_code=event_code,
            detail=detail,
            direction=direction,
            can_id=can_id,
            payload=payload,
        )

    def connect_can(self, interface, channel, bitrate):
        try:
            self.can_session.connect(interface, channel, bitrate)

            self.can_connection_panel.show_connected(
                interface, channel, bitrate
            )
            self.can_health.reset_connection_state()
            self.event_log_panel.reset_sync()

            self.set_can_health(
                "WARN",
                "WAIT_RX",
                f"{interface}/{channel} connected; waiting for STM32 frames"
            )

        except Exception as e:
            self.can_connection_panel.show_disconnected()
            self.set_can_health(
                "FAULT",
                "CONNECT_FAILED",
                type(e).__name__,
                str(e)
            )
            QMessageBox.critical(self, "CAN Connection Error", str(e))

    def set_can_health(self, severity, code, detail, tooltip=None):
        self.can_health.set_health(severity, code, detail, tooltip)

    def send_can_command(self, data) -> bool:
        result = self.can_session.send_command(data)

        if result.ok:
            return True

        if result.error_code == "DISCONNECTED":
            QMessageBox.warning(self, "Warning", "Önce CAN bağlantısını aç.")
        elif result.error_code == "INVALID_DLC":
            QMessageBox.critical(self, "Error", "CAN data 8 byte olmalı.")
        elif result.error_code == "TX_EXCEPTION":
            QMessageBox.critical(self, "CAN Send Error", result.message)

        return False

    def configure_and_start_slot(self, slot_no, can_id_text, id_type_text,
                                 cycle_time, counter):
        try:
            self.can_app_controller.configure_and_start_slot(
                slot_no=slot_no,
                can_id_text=can_id_text,
                id_type_text=id_type_text,
                cycle_time=cycle_time,
                counter=counter,
            )
        except SlotConfigurationError as error:
            QMessageBox.warning(self, error.title, error.detail)
        except ValueError:
            QMessageBox.warning(self, "Invalid Input", "CAN ID formatı hatalı. Örn: 0x123 veya 18FF50E5")
        except Exception as e:
            QMessageBox.critical(self, "Error", str(e))

    def send_led_command(self, led_no: int, state: int):
        self.can_app_controller.send_led_command(led_no, state)

    def send_pwm_command(self, frequency_hz, duty_percent, enabled=True):
        try:
            return self.can_app_controller.send_pwm_command(
                frequency_hz, duty_percent, enabled
            )
        except ValueError as error:
            QMessageBox.warning(self, "Invalid PWM Setting", str(error))
            return False

    def send_pwm_self_test(self, start=True):
        return self.can_app_controller.send_pwm_self_test(start)

    def handle_application_message(self, msg):
        if self.rtc_controller.handle_message(msg):
            return

        if self.can_app_controller.handle_message(msg):
            return

        if msg.arbitration_id in {
                STM32_LOG_RESPONSE_RX_ID,
                STM32_LOG_HEARTBEAT_RX_ID}:
            self.event_log_panel.handle_message(msg)
    
    def send_rtc_alarm(self):
        result = self.rtc_controller.send_alarm(
            **self.rtc_panel.get_alarm_request()
        )

        if result == "NO_FIELDS":
            QMessageBox.warning(
                self,
                "Alarm Alanı Seçilmedi",
                "En az bir karşılaştırma alanını seçin veya Alarmı Kapat düğmesini kullanın."
            )

    def disable_rtc_alarm(self):
        self.rtc_controller.disable_alarm()

    def send_rtc_set_time(self):
        try:
            _sent, weekday = self.rtc_controller.send_datetime(
                **self.rtc_panel.get_datetime_request()
            )
        except ValueError as error:
            QMessageBox.warning(
                self,
                "Geçersiz Tarih",
                f"Takvim kombinasyonu geçersiz: {error}"
            )
            return

        if self.rtc_panel.auto_weekday_checkbox.isChecked():
            self.rtc_panel.set_datetime_weekday(weekday)

    def closeEvent(self, event):
        self.write_event_log(
            source="APPLICATION",
            severity="INFO",
            event_code="STOP",
            detail="STM32 CAN GUI closing"
        )

        shutdown_error = self.can_session.shutdown()

        if shutdown_error is not None:
            self.write_event_log(
                source="CAN",
                severity="WARN",
                event_code="SHUTDOWN_FAILED",
                detail=(
                    f"{type(shutdown_error).__name__}: {shutdown_error}"
                )
            )

        event.accept()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    apply_application_theme(app)

    window = CanGui()
    window.setMinimumSize(920, 620)
    window.resize(1080, 780)
    window.show()

    sys.exit(app.exec())
