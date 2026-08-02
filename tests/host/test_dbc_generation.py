#!/usr/bin/env python3
"""Host contract tests for the generated CAN database."""

import copy
import re
import sys
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
PROTOCOL_DIRECTORY = REPOSITORY_ROOT / "protocol"
sys.path.insert(0, str(PROTOCOL_DIRECTORY))

from generate_dbc import (  # noqa: E402
    DBC_EXTENDED_ID_FLAG,
    dbc_name,
    encoded_frame_id,
    generate_dbc_text,
    load_definition,
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_generation_error(definition: dict, message: str) -> None:
    try:
        generate_dbc_text(definition)
    except ValueError:
        return
    raise AssertionError(message)


def main() -> None:
    definition = load_definition(PROTOCOL_DIRECTORY / "can_protocol.yaml")
    generated = generate_dbc_text(definition)
    committed = (PROTOCOL_DIRECTORY / "can_gui.dbc").read_text(
        encoding="utf-8"
    )
    require(generated == committed, "committed DBC is stale")
    require("\r" not in generated, "DBC output must use deterministic LF")
    require("PayloadByte" not in generated,
            "DBC must not contain generic payload-byte signals")

    for message_key, signals in definition["dbc_signals"].items():
        for signal in signals:
            if "reserved" in signal["name"].lower():
                require(
                    signal.get("maximum") == 0,
                    f"{message_key}.{signal['name']} must be constrained to zero",
                )

    identifiers = definition["identifiers"]
    message_lines = re.findall(r"(?m)^BO_ (\d+) ([A-Z0-9_]+):", generated)
    require(len(message_lines) == len(identifiers),
            "DBC must contain every protocol identifier exactly once")

    message_ids = {int(frame_id) for frame_id, _name in message_lines}
    expected_ids = {
        encoded_frame_id(message) for message in identifiers.values()
    }
    require(message_ids == expected_ids, "DBC frame IDs do not match YAML")

    gui_command = identifiers["gui_command"]
    gui_frame_id = encoded_frame_id(gui_command)
    require(gui_frame_id & DBC_EXTENDED_ID_FLAG,
            "GUI command must retain its extended-frame marker")

    for key, message in identifiers.items():
        frame_id = encoded_frame_id(message)
        message_name = dbc_name(key)
        transmitter = (
            "GUI" if message["direction"] == "gui_to_mcu" else "MCU"
        )
        receiver = "MCU" if message["direction"] == "gui_to_mcu" else "GUI"
        header = (
            f"BO_ {frame_id} {message_name}: "
            f"{definition['protocol']['payload_size']} {transmitter}"
        )
        require(header in generated, f"missing DBC message {message_name}")
        block = generated.split(header, maxsplit=1)[1].split("\n\n", maxsplit=1)[0]
        signal_lines = [
            line for line in block.splitlines() if line.startswith(" SG_ ")
        ]
        require(signal_lines, f"{message_name} must expose signals")
        require(
            all(line.endswith(f" {receiver}") for line in signal_lines),
            f"{message_name} signal receiver does not match direction",
        )
        common_bits = set()
        multiplex_bits = {}
        multiplexor_seen = False
        for signal_line in signal_lines:
            match = re.match(
                r" SG_ ([A-Za-z_][A-Za-z0-9_]*)(?: (M|m\d+))? : "
                r"(\d+)\|(\d+)@1[+-] ",
                signal_line,
            )
            require(match is not None,
                    f"invalid signal syntax in {message_name}")
            multiplex = match.group(2)
            start_bit = int(match.group(3))
            length = int(match.group(4))
            bits = set(range(start_bit, start_bit + length))
            if multiplex is not None and multiplex.startswith("m"):
                multiplex_value = int(multiplex[1:])
                group_bits = multiplex_bits.setdefault(
                    multiplex_value, set()
                )
                require(not bits & common_bits and not bits & group_bits,
                        f"overlapping signals in {message_name}")
                group_bits.update(bits)
            else:
                require(
                    not bits & common_bits and
                    all(not bits & group for group in multiplex_bits.values()),
                    f"overlapping signals in {message_name}",
                )
                common_bits.update(bits)
                multiplexor_seen |= multiplex == "M"
        payload_bits = set(range(
            definition["protocol"]["payload_size"] * 8
        ))
        if multiplex_bits:
            require(multiplexor_seen,
                    f"{message_name} multiplexor is missing")
            require(
                all(common_bits | group == payload_bits
                    for group in multiplex_bits.values()),
                f"{message_name} multiplex groups must cover the payload",
            )
        else:
            require(
                common_bits == payload_bits,
                f"{message_name} signals must cover every payload bit",
            )
        description = message.get("description", "")
        require(description in generated,
                f"missing DBC comment for {message_name}")

    product_signals = {
        "diagnostic_request": {
            "PciParameter", "PciType", "TransportData",
        },
        "diagnostic_response": {
            "PciParameter", "PciType", "TransportData",
        },
        "rtc_status": {
            "StatusCode", "HalStatus", "HalError", "Reserved",
        },
        "rtc_time": {
            "Hour", "Minute", "Second", "Hundredth", "Day", "Month",
            "Year", "Weekday", "TimeReserved", "CalendarValid", "Ready",
            "OscillatorStopFlag",
        },
        "system_status": {
            "Slot1Running", "Slot2Running", "Led1On", "Led2On",
            "SummaryReserved", "Slot1State", "Slot2State", "Led1State",
            "Led2State", "Slot1Requested", "Slot2Requested",
            "Led1Requested", "Led2Requested", "PwmRequested",
            "RequestReserved", "SwitchDataValid", "In0Closed", "In1Closed",
            "In2Closed", "In3Closed", "PhysicalReserved", "Slot1Blocked",
            "Slot2Blocked", "Led1Overridden", "OverrideReserved3",
            "PwmBlocked", "OverrideReserved5",
        },
        "rtc_alarm_event": {
            "EventCode", "AlarmFlag", "InterruptEnabled",
            "ConfigurationValid", "AlarmFlagsReserved", "Hour", "Minute",
            "Second", "Day", "Month", "Year",
        },
        "pwm_status": {
            "Running", "DutyCycle", "ActualFrequency", "Requested",
            "PhysicalPermitted", "Blocked", "SwitchDataValid",
            "ControlReserved", "Reserved",
        },
        "input_capture_status": {
            "SignalDetected", "DutyCycle", "Frequency", "EdgeCount",
        },
        "pwm_self_test_status": {
            "State", "CurrentPoint", "TotalPoints", "PassedPoints",
            "ExpectedFrequency",
        },
        "pwm_self_test_result": {
            "Point", "Passed", "ExpectedDutyCycle", "MeasuredDutyCycle",
            "MeasuredFrequency",
        },
        "log_response": {
            "FragmentHeader", "FragmentData",
        },
        "log_heartbeat": {
            "ProtocolVersion", "Ready", "OverwriteDetected",
            "HeartbeatFlagsReserved", "LatestSequence", "RecordCount",
            "AliveCounter",
        },
        "command_ack": {
            "ProtocolVersion", "CommandCode", "RequestToken", "AckStatus",
            "Executed", "AccessOpen", "AckFlagsReserved",
            "AccessWindowRemaining", "Reserved",
        },
        "tic12400_status": {
            "Online", "ConfigurationValid", "CrcComplete", "Monitoring",
            "StatusReserved4", "PorObserved", "ServiceFault",
            "StatusReserved7", "DeviceId", "ServiceResult", "SpiFail",
            "ParityFail", "SwitchStateChange", "SupplyThreshold",
            "Temperature", "OtherInterrupt", "PowerOnReset",
            "TransactionReserved", "ServiceFailureCount",
            "LastInterruptStatus",
        },
        "tic12400_switch_state": {
            "ClosedBitmap", "ValidBitmap", "Generation", "DataValid",
            "Reserved",
        },
        "tic12400_profile": {
            "BatterySwitchBitmap", "BatteryCapableBitmap", "Generation",
            "ConfigurationValid", "Reserved",
        },
        "can_rx_health": {
            "WatermarkSeen", "FifoFullSeen", "MessageLost",
            "BudgetExhausted", "HalError", "HealthFlagsReserved",
            "MaximumFifoFill", "MessageLostEvents", "FifoFullEvents",
            "WatermarkEvents",
        },
        "timing_service": {
            "ServiceId", "Enabled", "CurrentOverrun", "OverrunLatched",
            "TimingFlagsReserved", "CurrentExecutionTime",
            "MinimumExecutionTime", "MaximumExecutionTime",
        },
        "timing_ack_latency": {
            "P50Latency", "P95Latency", "P99Latency", "MaximumLatency",
        },
    }
    require(
        set(product_signals) == set(identifiers) - {"gui_command"},
        "every non-command frame must have explicit DBC signals",
    )
    for key, expected_signals in product_signals.items():
        frame_id = encoded_frame_id(identifiers[key])
        header = f"BO_ {frame_id} {dbc_name(key)}:"
        block = generated.split(header, maxsplit=1)[1].split(
            "\n\n", maxsplit=1
        )[0]
        names = set(re.findall(
            r"(?m)^ SG_ ([A-Za-z_][A-Za-z0-9_]*)", block
        ))
        require(names == expected_signals,
                f"{key} product signals do not match the wire contract")
        require("PayloadByte" not in block,
                f"{key} must not expose generic payload bytes")

    gui_header = f"BO_ {gui_frame_id} {dbc_name('gui_command')}:"
    gui_block = generated.split(gui_header, maxsplit=1)[1].split(
        "\n\n", maxsplit=1
    )[0]
    require(" SG_ CommandCode M : 0|8@1+" in gui_block,
            "GUI command code must be the DBC multiplexor")
    require("PayloadByte" not in gui_block,
            "GUI command must not expose generic payload bytes")
    gui_mux_values = {
        int(value) for value in re.findall(r"(?m)^ SG_ \w+ m(\d+) :", gui_block)
    }
    require(
        gui_mux_values == {
            command["code"] for command in definition["commands"].values()
        },
        "GUI multiplex groups must cover every command code",
    )
    command_signal_examples = {
        "set_slot_1": "Slot1CanId",
        "set_slot_2": "Slot2CanId",
        "led_control": "LedChannel",
        "start_slot_1_counter": "Slot1CounterLimit",
        "start_slot_2_counter": "Slot2CounterLimit",
        "rtc_set_time": "RtcTimeHour",
        "rtc_set_datetime": "RtcDateTimeMonth",
        "rtc_set_alarm": "AlarmSecondEnabled",
        "log_get_info": "LogGetInfoReserved",
        "log_read_sequence": "LogReadSequence",
        "pwm_set": "PwmFrequency",
        "pwm_self_test": "PwmSelfTestStart",
        "tic12400_set_polarity": "Tic12400BatterySwitchBitmap",
    }
    for key, signal_name in command_signal_examples.items():
        code = definition["commands"][key]["code"]
        require(f" SG_ {signal_name} m{code} :" in gui_block,
                f"missing multiplexed DBC payload for {key}")

    value_table = next(
        line for line in generated.splitlines()
        if line.startswith(f"VAL_ {gui_frame_id} CommandCode ")
    )
    for key, command in definition["commands"].items():
        entry = f'{command["code"]} "{dbc_name(key)}"'
        require(entry in value_table, f"missing command value {key}")

    ack_frame_id = encoded_frame_id(identifiers["command_ack"])
    ack_value_table = next(
        line for line in generated.splitlines()
        if line.startswith(f"VAL_ {ack_frame_id} AckStatus ")
    )
    require('4 "ACCESS_DENIED"' in ack_value_table,
            "missing command ACK status values")
    tic_status_frame_id = encoded_frame_id(identifiers["tic12400_status"])
    tic_result_table = next(
        line for line in generated.splitlines()
        if line.startswith(f"VAL_ {tic_status_frame_id} ServiceResult ")
    )
    require('5 "DEVICE_SPI_ERROR"' in tic_result_table,
            "missing TIC12400 service result values")
    rtc_status_frame_id = encoded_frame_id(identifiers["rtc_status"])
    rtc_status_table = next(
        line for line in generated.splitlines()
        if line.startswith(f"VAL_ {rtc_status_frame_id} StatusCode ")
    )
    require('242 "WRITE_VERIFY_MISMATCH"' in rtc_status_table,
            "missing RTC status values")
    pwm_test_frame_id = encoded_frame_id(
        identifiers["pwm_self_test_status"]
    )
    pwm_state_table = next(
        line for line in generated.splitlines()
        if line.startswith(f"VAL_ {pwm_test_frame_id} State ")
    )
    require('5 "ERROR"' in pwm_state_table,
            "missing PWM self-test state values")
    timing_frame_id = encoded_frame_id(identifiers["timing_service"])
    timing_service_table = next(
        line for line in generated.splitlines()
        if line.startswith(f"VAL_ {timing_frame_id} ServiceId ")
    )
    require('7 "WATCHDOG"' in timing_service_table,
            "missing timing service values")

    duplicate = copy.deepcopy(definition)
    duplicate["identifiers"]["duplicate"] = copy.deepcopy(gui_command)
    require_generation_error(duplicate, "duplicate CAN IDs must be rejected")

    overlapping = copy.deepcopy(definition)
    overlapping["dbc_signals"]["command_ack"][1]["start_bit"] = 0
    require_generation_error(overlapping,
                             "overlapping DBC signals must be rejected")

    outside = copy.deepcopy(definition)
    outside["dbc_signals"]["command_ack"][-1]["start_bit"] = 60
    require_generation_error(outside,
                             "out-of-frame DBC signals must be rejected")

    incomplete = copy.deepcopy(definition)
    incomplete["dbc_signals"]["command_ack"].pop()
    require_generation_error(incomplete,
                             "incomplete DBC signal coverage must be rejected")

    missing_mux_group = copy.deepcopy(definition)
    missing_code = missing_mux_group["commands"]["led_control"]["code"]
    missing_mux_group["dbc_signals"]["gui_command"] = [
        signal for signal in missing_mux_group["dbc_signals"]["gui_command"]
        if signal.get("multiplex_value") != missing_code
    ]
    require_generation_error(missing_mux_group,
                             "missing GUI multiplex groups must be rejected")

    invalid_mux_overlap = copy.deepcopy(definition)
    invalid_mux_overlap["dbc_signals"]["gui_command"][4]["start_bit"] = 8
    require_generation_error(invalid_mux_overlap,
                             "same-group multiplex overlaps must be rejected")

    missing_multiplexor = copy.deepcopy(definition)
    missing_multiplexor["dbc_signals"]["gui_command"][0]["multiplexor"] = False
    require_generation_error(missing_multiplexor,
                             "missing DBC multiplexors must be rejected")

    unknown_source = copy.deepcopy(definition)
    unknown_source["dbc_signals"]["command_ack"][0]["value_source"] = (
        "missing_values"
    )
    require_generation_error(unknown_source,
                             "unknown DBC value sources must be rejected")

    invalid_range = copy.deepcopy(definition)
    invalid_range["dbc_signals"]["rtc_time"][0]["maximum"] = 256
    require_generation_error(invalid_range,
                             "unrepresentable DBC ranges must be rejected")

    invalid_fragments = copy.deepcopy(definition)
    invalid_fragments["log_transport"]["record_fragment_count"] = 4
    require_generation_error(invalid_fragments,
                             "undersized log fragmentation must be rejected")

    overlapping_headers = copy.deepcopy(definition)
    overlapping_headers["log_transport"]["record_fragment_base"] = 0x71
    require_generation_error(overlapping_headers,
                             "overlapping log headers must be rejected")

    unknown_message = copy.deepcopy(definition)
    unknown_message["dbc_signals"]["missing_frame"] = copy.deepcopy(
        unknown_message["dbc_signals"]["command_ack"]
    )
    require_generation_error(unknown_message,
                             "unknown DBC signal messages must be rejected")

    missing_message = copy.deepcopy(definition)
    missing_message["dbc_signals"].pop("rtc_status")
    require_generation_error(missing_message,
                             "identifiers without DBC signals must be rejected")

    print(
        "PASS: DBC covers all frames and validates product signal contracts"
    )


if __name__ == "__main__":
    main()
