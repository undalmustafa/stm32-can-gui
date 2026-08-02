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
        occupied_bits = set()
        for signal_line in signal_lines:
            match = re.match(
                r" SG_ ([A-Za-z_][A-Za-z0-9_]*)(?: M)? : "
                r"(\d+)\|(\d+)@1[+-] ",
                signal_line,
            )
            require(match is not None,
                    f"invalid signal syntax in {message_name}")
            start_bit = int(match.group(2))
            length = int(match.group(3))
            bits = set(range(start_bit, start_bit + length))
            require(not bits & occupied_bits,
                    f"overlapping signals in {message_name}")
            occupied_bits.update(bits)
        require(
            occupied_bits == set(range(
                definition["protocol"]["payload_size"] * 8
            )),
            f"{message_name} signals must cover every payload bit",
        )
        description = message.get("description", "")
        require(description in generated,
                f"missing DBC comment for {message_name}")

    product_signals = {
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
    }
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

    unknown_source = copy.deepcopy(definition)
    unknown_source["dbc_signals"]["command_ack"][0]["value_source"] = (
        "missing_values"
    )
    require_generation_error(unknown_source,
                             "unknown DBC value sources must be rejected")

    unknown_message = copy.deepcopy(definition)
    unknown_message["dbc_signals"]["missing_frame"] = copy.deepcopy(
        unknown_message["dbc_signals"]["command_ack"]
    )
    require_generation_error(unknown_message,
                             "unknown DBC signal messages must be rejected")

    print(
        "PASS: DBC covers all frames and validates product signal contracts"
    )


if __name__ == "__main__":
    main()
