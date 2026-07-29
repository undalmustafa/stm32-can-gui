"""TIC12400 health and debounced open/closed switch controller."""

import time

from .protocol import (
    CMD_TIC12400_SET_POLARITY,
    TIC12400_ADC_CHANNEL_COUNT,
    TIC12400_BATTERY_CAPABLE_MASK,
    TIC12400_PROFILE_CONFIGURATION_VALID,
    TIC12400_PROFILE_RX_ID,
    TIC12400_RESULT_NAMES,
    TIC12400_STATUS_ADC_CHARACTERIZATION,
    TIC12400_STATUS_CONFIGURATION_VALID,
    TIC12400_STATUS_CRC_COMPLETE,
    TIC12400_STATUS_MONITORING,
    TIC12400_STATUS_ONLINE,
    TIC12400_STATUS_POR_OBSERVED,
    TIC12400_STATUS_RX_ID,
    TIC12400_STATUS_SERVICE_FAULT,
    TIC12400_SWITCH_DATA_VALID,
    TIC12400_SWITCH_STATE_RX_ID,
    TIC12400_TRANSACTION_OTHER_INTERRUPT,
    TIC12400_TRANSACTION_PARITY_FAIL,
    TIC12400_TRANSACTION_POWER_ON_RESET,
    TIC12400_TRANSACTION_SPI_FAIL,
    TIC12400_TRANSACTION_SUPPLY_THRESHOLD,
    TIC12400_TRANSACTION_SWITCH_STATE_CHANGE,
    TIC12400_TRANSACTION_TEMPERATURE,
)


class Tic12400Controller:
    """Decode only confirmed firmware switch state for the end-user view."""

    NOT_FITTED_CHANNEL = 12
    FITTED_MASK = (
        (1 << TIC12400_ADC_CHANNEL_COUNT) - 1
    ) & ~(1 << NOT_FITTED_CHANNEL)
    BATTERY_CAPABLE_MASK = TIC12400_BATTERY_CAPABLE_MASK

    def __init__(self, renderer, command_sender=None, clock=None):
        self._renderer = renderer
        self._command_sender = command_sender
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
                "state": (
                    "UNAVAILABLE"
                    if channel != self.NOT_FITTED_CHANNEL
                    else "NOT_FITTED"
                ),
                "polarity": "UNAVAILABLE",
            }
            for channel in range(TIC12400_ADC_CHANNEL_COUNT)
        ]
        self.switch_state = {
            "received": False,
            "data_valid": False,
            "closed_bitmap": 0,
            "valid_mask": 0,
            "generation": None,
            "malformed_frames": 0,
            "stale_frames": 0,
            "updated_at": None,
        }
        self.profile = {
            "received": False,
            "configuration_valid": False,
            "battery_switch_mask": 0,
            "battery_capable_mask": self.BATTERY_CAPABLE_MASK,
            "generation": None,
            "malformed_frames": 0,
            "updated_at": None,
        }

    def render(self):
        self._renderer(
            device_status=self.device_status,
            channels=self.channels,
            switch_state=self.switch_state,
            profile=self.profile,
        )

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

        if msg.arbitration_id == TIC12400_SWITCH_STATE_RX_ID:
            self._handle_switch_state(msg)
            return True

        if msg.arbitration_id == TIC12400_PROFILE_RX_ID:
            self._handle_profile(msg)
            return True

        return False

    def request_polarity(self, battery_switch_mask):
        battery_switch_mask = int(battery_switch_mask)
        if ((battery_switch_mask < 0) or
                (battery_switch_mask & ~self.BATTERY_CAPABLE_MASK)):
            raise ValueError(
                "Only IN0-IN9 support battery-connected polarity"
            )
        if self._command_sender is None:
            return False

        return self._command_sender([
            CMD_TIC12400_SET_POLARITY,
            battery_switch_mask & 0xFF,
            (battery_switch_mask >> 8) & 0xFF,
            (battery_switch_mask >> 16) & 0xFF,
            0,
            0,
            0,
            0,
        ])

    def _handle_status(self, msg):
        data = list(msg.data)
        if len(data) != 8:
            self.switch_state["malformed_frames"] += 1
            return

        flags = data[0]
        transaction = data[3]
        result = data[2]
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
            "updated_at": self._clock(),
        }
        self.render()

    def _handle_switch_state(self, msg):
        data = list(msg.data)
        if len(data) != 8:
            self.switch_state["malformed_frames"] += 1
            return

        closed_bitmap = int.from_bytes(bytes(data[0:3]), "little")
        valid_mask = int.from_bytes(bytes(data[3:6]), "little")
        generation = data[6]
        flags = data[7]
        data_valid = bool(flags & TIC12400_SWITCH_DATA_VALID)

        if ((flags & ~TIC12400_SWITCH_DATA_VALID) != 0 or
                (closed_bitmap & ~valid_mask) != 0 or
                (data_valid and valid_mask != self.FITTED_MASK)):
            self.switch_state["malformed_frames"] += 1
            return

        current_generation = self.switch_state["generation"]
        if (generation != current_generation and
                not self._is_newer_generation(
                    generation, current_generation)):
            self.switch_state["stale_frames"] += 1
            return

        self.switch_state.update({
            "received": True,
            "data_valid": data_valid,
            "closed_bitmap": closed_bitmap,
            "valid_mask": valid_mask,
            "generation": generation,
            "updated_at": self._clock(),
        })
        for channel in self.channels:
            index = channel["channel"]
            bit = 1 << index
            if not channel["fitted"]:
                channel["state"] = "NOT_FITTED"
            elif data_valid and (valid_mask & bit):
                channel["state"] = (
                    "CLOSED" if (closed_bitmap & bit) else "OPEN"
                )
            else:
                channel["state"] = "UNAVAILABLE"

        self.render()

    def _handle_profile(self, msg):
        data = list(msg.data)
        if len(data) != 8:
            self.profile["malformed_frames"] += 1
            return

        battery_switch_mask = int.from_bytes(
            bytes(data[0:3]), "little"
        )
        battery_capable_mask = int.from_bytes(
            bytes(data[3:6]), "little"
        )
        flags = data[7]

        if ((flags & ~TIC12400_PROFILE_CONFIGURATION_VALID) != 0 or
                (battery_capable_mask & ~self.FITTED_MASK) != 0 or
                (battery_switch_mask & ~battery_capable_mask) != 0):
            self.profile["malformed_frames"] += 1
            return

        configuration_valid = bool(
            flags & TIC12400_PROFILE_CONFIGURATION_VALID
        )
        self.profile.update({
            "received": True,
            "configuration_valid": configuration_valid,
            "battery_switch_mask": battery_switch_mask,
            "battery_capable_mask": battery_capable_mask,
            "generation": data[6],
            "updated_at": self._clock(),
        })

        for channel in self.channels:
            index = channel["channel"]
            bit = 1 << index
            if not channel["fitted"]:
                channel["polarity"] = "NOT_FITTED"
            elif not configuration_valid:
                channel["polarity"] = "UNAVAILABLE"
            elif battery_switch_mask & bit:
                channel["polarity"] = "BATTERY"
            else:
                channel["polarity"] = "GROUND"

        self.render()
