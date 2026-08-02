"""Periodic Qt timers used by the STM32 CAN GUI application."""

from PySide6.QtCore import Qt, QTimer


CAN_RX_POLL_PERIOD_MS = 50
CAN_HEALTH_POLL_PERIOD_MS = 250
STM32_LOG_SYNC_PERIOD_MS = 50
DIAGNOSTIC_POLL_PERIOD_MS = 1000


class ApplicationTimers:
    """Create and retain the application's periodic timer objects."""

    def __init__(self, can_rx_poll, can_health_poll, stm32_log_sync,
                 diagnostic_poll=None):
        self.can_rx_timer = self._start_timer(
            CAN_RX_POLL_PERIOD_MS,
            can_rx_poll,
        )
        self.can_health_timer = self._start_timer(
            CAN_HEALTH_POLL_PERIOD_MS,
            can_health_poll,
        )
        self.stm32_log_sync_timer = self._start_timer(
            STM32_LOG_SYNC_PERIOD_MS,
            stm32_log_sync,
        )
        self.diagnostic_timer = None
        if diagnostic_poll is not None:
            self.diagnostic_timer = self._start_timer(
                DIAGNOSTIC_POLL_PERIOD_MS,
                diagnostic_poll,
            )

    @staticmethod
    def _start_timer(period_ms, callback):
        timer = QTimer()
        timer.setTimerType(Qt.TimerType.PreciseTimer)
        timer.timeout.connect(callback)
        timer.start(period_ms)
        return timer
