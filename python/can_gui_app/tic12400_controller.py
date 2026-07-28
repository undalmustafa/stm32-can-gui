"""TIC12400 device-status and segmented raw-ADC telemetry controller."""

import csv
import io
import time

from .protocol import (
    TIC12400_ADC_CHANNEL_COUNT,
    TIC12400_ADC_CODE_MAX,
    TIC12400_ADC_CODES_PER_FRAME,
    TIC12400_ADC_GROUP_COUNT,
    TIC12400_ADC_RX_ID,
    TIC12400_RESULT_NAMES,
    TIC12400_STATUS_ADC_CHARACTERIZATION,
    TIC12400_STATUS_CONFIGURATION_VALID,
    TIC12400_STATUS_CRC_COMPLETE,
    TIC12400_STATUS_MONITORING,
    TIC12400_STATUS_ONLINE,
    TIC12400_STATUS_POR_OBSERVED,
    TIC12400_STATUS_RX_ID,
    TIC12400_STATUS_SERVICE_FAULT,
    TIC12400_TRANSACTION_OTHER_INTERRUPT,
    TIC12400_TRANSACTION_PARITY_FAIL,
    TIC12400_TRANSACTION_POWER_ON_RESET,
    TIC12400_TRANSACTION_SPI_FAIL,
    TIC12400_TRANSACTION_SUPPLY_THRESHOLD,
    TIC12400_TRANSACTION_SWITCH_STATE_CHANGE,
    TIC12400_TRANSACTION_TEMPERATURE,
)


class Tic12400Controller:
    """Decode TIC12400 telemetry while retaining per-channel freshness."""

    NOT_FITTED_CHANNEL = 12
    COMPLETE_GROUP_MASK = (1 << TIC12400_ADC_GROUP_COUNT) - 1
    CALIBRATION_POSITIONS = ("left", "center", "right")

    def __init__(self, renderer, clock=None, calibration_sample_target=10):
        if calibration_sample_target < 1:
            raise ValueError("calibration_sample_target must be positive")

        self._renderer = renderer
        self._clock = clock or time.monotonic
        self.device_status = {
            "received": False,
            "healthy": None,
            "online": False,
            "configuration_valid": False,
            "crc_complete": False,
            "monitoring": False,
            "adc_characterization": False,
            "por_observed": False,
            "service_fault": False,
            "device_id": None,
            "service_result": None,
            "service_result_name": "UNKNOWN",
            "transaction_flags": {},
            "service_failures": 0,
            "last_nonzero_int_status": 0,
            "updated_at": None,
        }
        self.channels = [
            {
                "channel": channel,
                "fitted": channel != self.NOT_FITTED_CHANNEL,
                "adc_code": None,
                "generation": None,
                "state": (
                    "UNCHARACTERIZED"
                    if channel != self.NOT_FITTED_CHANNEL
                    else "NOT_FITTED"
                ),
                "updated_at": None,
            }
            for channel in range(TIC12400_ADC_CHANNEL_COUNT)
        ]
        self.telemetry = {
            "generation": None,
            "received_group_mask": 0,
            "received_group_count": 0,
            "snapshot_complete": False,
            "complete_generation": None,
            "malformed_frames": 0,
            "stale_frames": 0,
            "duplicate_groups": 0,
            "last_adc_update_at": None,
        }
        self.calibration = self._new_calibration_state(
            calibration_sample_target
        )

    def render(self):
        self._renderer(
            device_status=self.device_status,
            channels=self.channels,
            telemetry=self.telemetry,
            calibration=self.calibration,
        )

    @classmethod
    def _new_position_capture(cls):
        return {
            "sample_count": 0,
            "completed": False,
            "first_generation": None,
            "last_generation": None,
            "minimum": [None] * TIC12400_ADC_CHANNEL_COUNT,
            "maximum": [None] * TIC12400_ADC_CHANNEL_COUNT,
        }

    @classmethod
    def _new_calibration_state(cls, sample_target):
        return {
            "sample_target": sample_target,
            "active_position": None,
            "last_completed_position": None,
            "last_error": None,
            "positions": {
                position: cls._new_position_capture()
                for position in cls.CALIBRATION_POSITIONS
            },
        }

    def start_position_capture(self, position):
        normalized = str(position).strip().lower()
        if normalized not in self.CALIBRATION_POSITIONS:
            raise ValueError(f"invalid TIC12400 position: {position}")

        if not self._calibration_ready():
            self.calibration["last_error"] = (
                "Capture requires online, configured, CRC-complete "
                "TIC12400 ADC monitoring without a service fault."
            )
            self.render()
            return False

        self.calibration["positions"][normalized] = (
            self._new_position_capture()
        )
        self.calibration["active_position"] = normalized
        self.calibration["last_error"] = None
        self.render()
        return True

    def clear_position_captures(self):
        sample_target = self.calibration["sample_target"]
        self.calibration = self._new_calibration_state(sample_target)
        self.render()

    def calibration_csv_text(self):
        output = io.StringIO(newline="")
        writer = csv.writer(output, lineterminator="\n")
        header = ["channel", "fitted"]
        for position in self.CALIBRATION_POSITIONS:
            header.extend((
                f"{position}_minimum",
                f"{position}_maximum",
                f"{position}_samples",
            ))
        writer.writerow(header)

        for channel in range(TIC12400_ADC_CHANNEL_COUNT):
            fitted = channel != self.NOT_FITTED_CHANNEL
            row = [f"IN{channel}", 1 if fitted else 0]
            for position in self.CALIBRATION_POSITIONS:
                capture = self.calibration["positions"][position]
                minimum = capture["minimum"][channel]
                maximum = capture["maximum"][channel]
                row.extend((
                    "" if minimum is None else minimum,
                    "" if maximum is None else maximum,
                    capture["sample_count"] if fitted else 0,
                ))
            writer.writerow(row)

        return output.getvalue()

    def _calibration_ready(self):
        return (
            self.device_status["received"]
            and self.device_status["online"]
            and self.device_status["configuration_valid"]
            and self.device_status["crc_complete"]
            and self.device_status["monitoring"]
            and self.device_status["adc_characterization"]
            and not self.device_status["service_fault"]
        )

    def _record_calibration_snapshot(self, generation):
        position = self.calibration["active_position"]
        if position is None:
            return

        if not self._calibration_ready():
            self.calibration["active_position"] = None
            self.calibration["last_error"] = (
                "Capture stopped because TIC12400 monitoring is not healthy."
            )
            return

        capture = self.calibration["positions"][position]
        fitted_channels = [
            channel for channel in self.channels if channel["fitted"]
        ]
        if any(
            channel["generation"] != generation
            or channel["adc_code"] is None
            for channel in fitted_channels
        ):
            return

        for channel in fitted_channels:
            index = channel["channel"]
            code = channel["adc_code"]
            minimum = capture["minimum"][index]
            maximum = capture["maximum"][index]
            capture["minimum"][index] = (
                code if minimum is None else min(minimum, code)
            )
            capture["maximum"][index] = (
                code if maximum is None else max(maximum, code)
            )

        if capture["sample_count"] == 0:
            capture["first_generation"] = generation
        capture["last_generation"] = generation
        capture["sample_count"] += 1

        if capture["sample_count"] >= self.calibration["sample_target"]:
            capture["completed"] = True
            self.calibration["active_position"] = None
            self.calibration["last_completed_position"] = position

    @staticmethod
    def _is_newer_generation(candidate, current):
        if current is None:
            return True

        delta = (candidate - current) & 0xFF
        return 0 < delta < 0x80

    def handle_message(self, msg):
        if msg.arbitration_id == TIC12400_STATUS_RX_ID:
            self._handle_status(msg)
            return True

        if msg.arbitration_id == TIC12400_ADC_RX_ID:
            self._handle_adc(msg)
            return True

        return False

    def _handle_status(self, msg):
        data = list(msg.data)
        if len(data) != 8:
            self.telemetry["malformed_frames"] += 1
            return

        flags = data[0]
        transaction = data[3]
        result = data[2]
        now = self._clock()
        online = bool(flags & TIC12400_STATUS_ONLINE)
        configuration_valid = bool(
            flags & TIC12400_STATUS_CONFIGURATION_VALID
        )
        crc_complete = bool(flags & TIC12400_STATUS_CRC_COMPLETE)
        monitoring = bool(flags & TIC12400_STATUS_MONITORING)
        service_fault = bool(flags & TIC12400_STATUS_SERVICE_FAULT)

        self.device_status = {
            "received": True,
            "healthy": (
                online
                and configuration_valid
                and crc_complete
                and monitoring
                and not service_fault
            ),
            "online": online,
            "configuration_valid": configuration_valid,
            "crc_complete": crc_complete,
            "monitoring": monitoring,
            "adc_characterization": bool(
                flags & TIC12400_STATUS_ADC_CHARACTERIZATION
            ),
            "por_observed": bool(flags & TIC12400_STATUS_POR_OBSERVED),
            "service_fault": service_fault,
            "device_id": data[1],
            "service_result": result,
            "service_result_name": TIC12400_RESULT_NAMES.get(
                result, f"UNKNOWN_0x{result:02X}"
            ),
            "transaction_flags": {
                "spi_fail": bool(
                    transaction & TIC12400_TRANSACTION_SPI_FAIL
                ),
                "parity_fail": bool(
                    transaction & TIC12400_TRANSACTION_PARITY_FAIL
                ),
                "switch_state_change": bool(
                    transaction &
                    TIC12400_TRANSACTION_SWITCH_STATE_CHANGE
                ),
                "supply_threshold": bool(
                    transaction &
                    TIC12400_TRANSACTION_SUPPLY_THRESHOLD
                ),
                "temperature": bool(
                    transaction & TIC12400_TRANSACTION_TEMPERATURE
                ),
                "other_interrupt": bool(
                    transaction &
                    TIC12400_TRANSACTION_OTHER_INTERRUPT
                ),
                "power_on_reset": bool(
                    transaction &
                    TIC12400_TRANSACTION_POWER_ON_RESET
                ),
            },
            "service_failures": int.from_bytes(
                bytes(data[4:6]), "little"
            ),
            "last_nonzero_int_status": int.from_bytes(
                bytes(data[6:8]), "little"
            ),
            "updated_at": now,
        }
        if (self.calibration["active_position"] is not None
                and not self._calibration_ready()):
            self.calibration["active_position"] = None
            self.calibration["last_error"] = (
                "Capture stopped because TIC12400 monitoring is not healthy."
            )
        elif self._calibration_ready():
            self.calibration["last_error"] = None
        self.render()

    def _handle_adc(self, msg):
        data = list(msg.data)
        if len(data) != 8:
            self.telemetry["malformed_frames"] += 1
            return

        generation = data[0]
        group_index = data[1]
        if group_index >= TIC12400_ADC_GROUP_COUNT:
            self.telemetry["malformed_frames"] += 1
            return

        codes = [
            int.from_bytes(bytes(data[offset:offset + 2]), "little")
            for offset in (2, 4, 6)
        ]
        if any(code > TIC12400_ADC_CODE_MAX for code in codes):
            self.telemetry["malformed_frames"] += 1
            return

        current_generation = self.telemetry["generation"]
        if generation != current_generation:
            if not self._is_newer_generation(
                    generation, current_generation):
                self.telemetry["stale_frames"] += 1
                return

            self.telemetry["generation"] = generation
            self.telemetry["received_group_mask"] = 0
            self.telemetry["received_group_count"] = 0
            self.telemetry["snapshot_complete"] = False

        group_bit = 1 << group_index
        if self.telemetry["received_group_mask"] & group_bit:
            self.telemetry["duplicate_groups"] += 1
            self.render()
            return
        else:
            self.telemetry["received_group_mask"] |= group_bit
            self.telemetry["received_group_count"] += 1

        now = self._clock()
        first_channel = group_index * TIC12400_ADC_CODES_PER_FRAME
        for code_index, code in enumerate(codes):
            channel_index = first_channel + code_index
            channel = self.channels[channel_index]
            if not channel["fitted"]:
                continue

            channel["adc_code"] = code
            channel["generation"] = generation
            channel["updated_at"] = now

        became_complete = (
            self.telemetry["received_group_mask"] ==
            self.COMPLETE_GROUP_MASK
            and not self.telemetry["snapshot_complete"]
        )
        if became_complete:
            self.telemetry["snapshot_complete"] = True
            self.telemetry["complete_generation"] = generation
            self._record_calibration_snapshot(generation)

        self.telemetry["last_adc_update_at"] = now
        self.render()
