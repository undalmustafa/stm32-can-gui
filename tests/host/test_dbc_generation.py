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
        require(
            len(signal_lines) == definition["protocol"]["payload_size"],
            f"{message_name} must expose every payload byte",
        )
        require(
            all(line.endswith(f" {receiver}") for line in signal_lines),
            f"{message_name} signal receiver does not match direction",
        )
        description = message.get("description", "")
        require(description in generated,
                f"missing DBC comment for {message_name}")

    value_table = next(
        line for line in generated.splitlines()
        if line.startswith(f"VAL_ {gui_frame_id} CommandCode ")
    )
    for key, command in definition["commands"].items():
        entry = f'{command["code"]} "{dbc_name(key)}"'
        require(entry in value_table, f"missing command value {key}")

    duplicate = copy.deepcopy(definition)
    duplicate["identifiers"]["duplicate"] = copy.deepcopy(gui_command)
    try:
        generate_dbc_text(duplicate)
    except ValueError:
        pass
    else:
        raise AssertionError("duplicate CAN IDs must be rejected")

    print(
        "PASS: DBC covers all identifiers, directions, comments, and commands"
    )


if __name__ == "__main__":
    main()
