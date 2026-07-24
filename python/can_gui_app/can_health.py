"""PCAN driver and STM32 application-traffic health state machine."""

import time

try:
    from can.interfaces.pcan.basic import (
        PCAN_ERROR_BUSHEAVY,
        PCAN_ERROR_BUSLIGHT,
        PCAN_ERROR_BUSOFF,
        PCAN_ERROR_BUSPASSIVE,
    )
except ImportError:
    # Stable PCAN-Basic status values; fallback for older python-can builds.
    PCAN_ERROR_BUSLIGHT = 0x00004
    PCAN_ERROR_BUSHEAVY = 0x00008
    PCAN_ERROR_BUSOFF = 0x00010
    PCAN_ERROR_BUSPASSIVE = 0x40000


STM32_RX_TIMEOUT_S = 1.5
PCAN_RECOVERY_RESET_DELAY_S = 0.5
PCAN_RECOVERY_MIN_STM32_FRAMES = 3


class CanHealthMonitor:
    def __init__(self,
                 bus_provider,
                 metrics_provider,
                 event_writer,
                 view_renderer):
        self._bus_provider = bus_provider
        self._metrics_provider = metrics_provider
        self._event_writer = event_writer
        self._view_renderer = view_renderer

        self.error_event_count = 0
        self.error_frame_count = 0
        self.active_issue = None
        self.last_logged_state = None
        self.warning_started_at = None
        self.warning_stm32_rx_count = 0
        self.warning_reset_attempted = False
        self.reset_succeeded_at = None
        self.reset_stm32_rx_count = 0
        self.reset_error_frame_count = 0
        self.latched_driver_status_recovered = False

    @property
    def bus(self):
        return self._bus_provider()

    @property
    def metrics(self):
        return self._metrics_provider()

    def reset_connection_state(self):
        self.error_event_count = 0
        self.error_frame_count = 0
        self.active_issue = None
        self.last_logged_state = None
        self.clear_recovery_state()

    def set_health(self, severity, code, detail, tooltip=None):
        if severity in {"WARN", "FAULT"} and code != "WAIT_RX":
            if self.active_issue != code:
                self.error_event_count += 1
            self.active_issue = code
        elif severity == "OK":
            self.active_issue = None

        metrics = self.metrics
        health_tooltip = tooltip or detail
        self._view_renderer(
            severity=severity,
            code=code,
            detail=detail,
            tooltip=health_tooltip,
            rx_count=metrics["rx_count"],
            error_event_count=self.error_event_count,
            rx_budget_hit_count=metrics["rx_budget_hit_count"],
            error_frame_count=self.error_frame_count,
        )

        health_state = (severity, code)

        if self.last_logged_state != health_state:
            self.last_logged_state = health_state
            log_detail = str(detail)

            if tooltip is not None and str(tooltip) != log_detail:
                log_detail += f" | {tooltip}"

            self._event_writer(
                source="CAN_HEALTH",
                severity=severity,
                event_code=code,
                detail=log_detail,
            )

    @staticmethod
    def get_bus_mode_name(bus):
        try:
            state = bus.state
        except Exception:
            return "UNKNOWN"

        return getattr(state, "name", str(state)).upper()

    def clear_recovery_state(self):
        stm32_rx_count = self.metrics["stm32_rx_count"]
        self.warning_started_at = None
        self.warning_stm32_rx_count = stm32_rx_count
        self.warning_reset_attempted = False
        self.reset_succeeded_at = None
        self.reset_stm32_rx_count = stm32_rx_count
        self.reset_error_frame_count = self.error_frame_count
        self.latched_driver_status_recovered = False

    def _try_reset_after_recovery(self, now, raw_status):
        metrics = self.metrics

        if self.warning_started_at is None:
            self.warning_started_at = now
            self.warning_stm32_rx_count = metrics["stm32_rx_count"]
            self.warning_reset_attempted = False
            return False

        if self.warning_reset_attempted:
            return False

        last_stm32_rx_time = metrics["last_stm32_rx_time"]
        recent_stm32_rx = (
            last_stm32_rx_time is not None
            and (now - last_stm32_rx_time) < STM32_RX_TIMEOUT_S
        )
        recovered_frame_count = (
            metrics["stm32_rx_count"] - self.warning_stm32_rx_count
        )
        last_log_response_rx_time = metrics.get(
            "last_log_response_rx_time"
        )
        log_round_trip_confirmed = (
            last_log_response_rx_time is not None
            and last_log_response_rx_time >= self.warning_started_at
        )
        warning_age = now - self.warning_started_at
        bus = self.bus

        if (
            not recent_stm32_rx
            or recovered_frame_count < PCAN_RECOVERY_MIN_STM32_FRAMES
            or warning_age < PCAN_RECOVERY_RESET_DELAY_S
            or bus is None
            or not hasattr(bus, "reset")
        ):
            return False

        self.warning_reset_attempted = True

        try:
            reset_ok = bool(bus.reset())
        except Exception as error:
            self._event_writer(
                source="CAN_HEALTH",
                severity="FAULT",
                event_code="PCAN_RESET_EXCEPTION",
                detail=f"{type(error).__name__}: {error}",
            )
            return False

        self._event_writer(
            source="CAN_HEALTH",
            severity="INFO" if reset_ok else "WARN",
            event_code=(
                "PCAN_RECOVERY_RESET_OK"
                if reset_ok
                else "PCAN_RECOVERY_RESET_FAILED"
            ),
            detail=(
                f"DRIVER_BEFORE=0x{raw_status:08X} "
                f"STM32_RX_CONFIRMED={recovered_frame_count} "
                "LOG_ROUND_TRIP="
                f"{'YES' if log_round_trip_confirmed else 'NO'}"
            ),
        )

        if reset_ok:
            self.reset_succeeded_at = now
            self.reset_stm32_rx_count = metrics["stm32_rx_count"]
            self.reset_error_frame_count = self.error_frame_count
            self.latched_driver_status_recovered = False

        return reset_ok

    def _latched_driver_status_is_recovered(self, now):
        metrics = self.metrics
        last_stm32_rx_time = metrics["last_stm32_rx_time"]
        recent_stm32_rx = (
            last_stm32_rx_time is not None
            and (now - last_stm32_rx_time) < STM32_RX_TIMEOUT_S
        )

        if self.latched_driver_status_recovered:
            return recent_stm32_rx

        if self.reset_succeeded_at is None:
            return False

        stable_age = now - self.reset_succeeded_at
        valid_frames_after_reset = (
            metrics["stm32_rx_count"] - self.reset_stm32_rx_count
        )
        no_new_error_frames = (
            self.error_frame_count == self.reset_error_frame_count
        )

        if (
            stable_age >= PCAN_RECOVERY_RESET_DELAY_S
            and valid_frames_after_reset >= PCAN_RECOVERY_MIN_STM32_FRAMES
            and no_new_error_frames
            and recent_stm32_rx
        ):
            self.latched_driver_status_recovered = True
            return True

        return False

    def observe_error_frame(self):
        self.error_frame_count += 1

        if self.latched_driver_status_recovered:
            self.latched_driver_status_recovered = False
            self.warning_started_at = time.monotonic()
            self.warning_stm32_rx_count = self.metrics["stm32_rx_count"]
            self.warning_reset_attempted = False
            self.reset_succeeded_at = None

    def monitor(self):
        bus = self.bus

        if bus is None:
            return

        now = time.monotonic()
        metrics = self.metrics
        mode_name = self.get_bus_mode_name(bus)
        raw_status = None
        status_ok = None
        status_description = None

        try:
            if hasattr(bus, "status"):
                raw_status = int(bus.status())
            if hasattr(bus, "status_is_ok"):
                status_ok = bool(bus.status_is_ok())
            if hasattr(bus, "status_string"):
                status_description = bus.status_string()
        except Exception as error:
            self.set_health(
                "FAULT", "STATUS_EXCEPTION", type(error).__name__, str(error)
            )
            return

        if raw_status is not None:
            if raw_status & int(PCAN_ERROR_BUSOFF):
                if self._try_reset_after_recovery(now, raw_status):
                    try:
                        raw_status = int(bus.status())
                        status_ok = raw_status == 0
                        status_description = bus.status_string()
                    except Exception:
                        raw_status = int(PCAN_ERROR_BUSOFF)

                if (raw_status & int(PCAN_ERROR_BUSOFF) and
                        self._latched_driver_status_is_recovered(now)):
                    rx_age_ms = (
                        now - metrics["last_stm32_rx_time"]
                    ) * 1000.0
                    self.set_health(
                        "OK", "RECOVERED_BUS_OFF",
                        f"STM32_RX_AGE={rx_age_ms:.0f} ms "
                        f"DRIVER_LATCHED=0x{raw_status:08X}",
                        "Bidirectional log traffic or current STM32 "
                        "application traffic confirms recovery. The PCAN "
                        "BUS-OFF status bit is historical.",
                    )
                    return
                elif raw_status & int(PCAN_ERROR_BUSOFF):
                    self.set_health(
                        "FAULT", "BUS_OFF",
                        f"MODE={mode_name} DRIVER=0x{raw_status:08X}",
                        status_description,
                    )
                    return

            if raw_status & int(PCAN_ERROR_BUSPASSIVE):
                self.set_health(
                    "WARN", "ERROR_PASSIVE",
                    f"MODE={mode_name} DRIVER=0x{raw_status:08X}",
                    status_description,
                )
                return

            if raw_status & int(PCAN_ERROR_BUSHEAVY):
                if self._try_reset_after_recovery(now, raw_status):
                    try:
                        raw_status = int(bus.status())
                        status_ok = raw_status == 0
                        status_description = bus.status_string()
                    except Exception:
                        raw_status = int(PCAN_ERROR_BUSHEAVY)

                if raw_status & int(PCAN_ERROR_BUSHEAVY):
                    if self._latched_driver_status_is_recovered(now):
                        rx_age_ms = (
                            now - metrics["last_stm32_rx_time"]
                        ) * 1000.0
                        self.set_health(
                            "OK", "ACTIVE",
                            f"STM32_RX_AGE={rx_age_ms:.0f} ms "
                            f"DRIVER_LATCHED=0x{raw_status:08X}",
                            "Current STM32 traffic is healthy and no new "
                            "PCAN error frame was observed after reset. "
                            "The driver BUSHEAVY bit is historical.",
                        )
                        return

                    self.set_health(
                        "WARN", "BUS_HEAVY",
                        f"MODE={mode_name} DRIVER=0x{raw_status:08X}",
                        status_description,
                    )
                    return

            if raw_status & int(PCAN_ERROR_BUSLIGHT):
                self._try_reset_after_recovery(now, raw_status)
                self.set_health(
                    "WARN", "ERROR_WARNING",
                    f"MODE={mode_name} DRIVER=0x{raw_status:08X}",
                    status_description,
                )
                return

            self.clear_recovery_state()

        if status_ok is False:
            status_text = (
                f"MODE={mode_name} DRIVER=0x{raw_status:08X}"
                if raw_status is not None
                else f"MODE={mode_name}"
            )
            self.set_health(
                "WARN", "DRIVER_WARNING", status_text, status_description
            )
            return

        rx_reference = (
            metrics["last_stm32_rx_time"] or metrics["connected_at"]
        )

        if rx_reference is None:
            self.set_health(
                "WARN", "WAIT_RX",
                f"MODE={mode_name}; waiting for STM32 frames",
            )
            return

        rx_age = now - rx_reference

        if metrics["last_stm32_rx_time"] is None:
            if rx_age >= STM32_RX_TIMEOUT_S:
                self.set_health(
                    "FAULT", "STM32_RX_TIMEOUT",
                    f"MODE={mode_name}; no application frame for {rx_age:.1f} s",
                )
            else:
                self.set_health(
                    "WARN", "WAIT_RX",
                    f"MODE={mode_name}; waiting for STM32 frames",
                )
            return

        if rx_age >= STM32_RX_TIMEOUT_S:
            self.set_health(
                "FAULT", "STM32_RX_TIMEOUT",
                f"MODE={mode_name}; last application RX {rx_age:.1f} s ago",
            )
            return

        self.set_health(
            "OK", mode_name, f"STM32_RX_AGE={rx_age * 1000.0:.0f} ms"
        )
