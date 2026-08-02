#!/usr/bin/env python3
"""Generate a deterministic CAN database from can_protocol.yaml."""

import re
import sys
from pathlib import Path

import yaml


DBC_EXTENDED_ID_FLAG = 0x80000000
DBC_NAME_PATTERN = re.compile(r"[^A-Za-z0-9_]")
DIRECTIONS = {
    "gui_to_mcu": ("GUI", "MCU"),
    "mcu_to_gui": ("MCU", "GUI"),
}


def dbc_name(value: str) -> str:
    name = DBC_NAME_PATTERN.sub("_", value)
    if not name or name[0].isdigit():
        name = f"CAN_{name}"
    return name.upper()


def dbc_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def load_definition(yaml_path: Path) -> dict:
    with yaml_path.open("r", encoding="utf-8") as stream:
        definition = yaml.safe_load(stream)
    if not isinstance(definition, dict):
        raise ValueError("protocol definition must be a mapping")
    return definition


def validate_definition(definition: dict) -> None:
    protocol = definition.get("protocol")
    identifiers = definition.get("identifiers")
    commands = definition.get("commands", {})

    if not isinstance(protocol, dict):
        raise ValueError("protocol section must be a mapping")
    payload_size = protocol.get("payload_size")
    if not isinstance(payload_size, int) or not 1 <= payload_size <= 64:
        raise ValueError("protocol payload_size must be in range 1..64")
    if not isinstance(identifiers, dict) or not identifiers:
        raise ValueError("identifiers section must be a non-empty mapping")

    encoded_ids = set()
    message_names = set()
    for key, message in identifiers.items():
        if not isinstance(message, dict):
            raise ValueError(f"identifier {key} must be a mapping")
        can_id = message.get("id")
        frame_type = message.get("type")
        direction = message.get("direction")
        message_name = dbc_name(key)

        if frame_type not in {"standard", "extended"}:
            raise ValueError(f"identifier {key} has invalid frame type")
        maximum_id = 0x7FF if frame_type == "standard" else 0x1FFFFFFF
        if not isinstance(can_id, int) or not 0 <= can_id <= maximum_id:
            raise ValueError(f"identifier {key} is outside {frame_type} range")
        if direction not in DIRECTIONS:
            raise ValueError(f"identifier {key} has invalid direction")

        encoded_id = can_id
        if frame_type == "extended":
            encoded_id |= DBC_EXTENDED_ID_FLAG
        if encoded_id in encoded_ids:
            raise ValueError(f"identifier {key} duplicates a CAN frame ID")
        if message_name in message_names:
            raise ValueError(f"identifier {key} duplicates a DBC message name")
        encoded_ids.add(encoded_id)
        message_names.add(message_name)

    command_codes = set()
    for key, command in commands.items():
        if not isinstance(command, dict):
            raise ValueError(f"command {key} must be a mapping")
        code = command.get("code")
        if not isinstance(code, int) or not 0 <= code <= 0xFF:
            raise ValueError(f"command {key} code must be in range 0..255")
        if code in command_codes:
            raise ValueError(f"command {key} duplicates a command code")
        command_codes.add(code)


def encoded_frame_id(message: dict) -> int:
    can_id = message["id"]
    if message["type"] == "extended":
        return can_id | DBC_EXTENDED_ID_FLAG
    return can_id


def generate_dbc_text(definition: dict) -> str:
    validate_definition(definition)
    protocol = definition["protocol"]
    identifiers = definition["identifiers"]
    payload_size = protocol["payload_size"]
    protocol_name = dbc_string(str(protocol.get("name", "can_protocol")))
    protocol_version = protocol.get("version", 1)

    lines = [
        f'VERSION "{protocol_name} protocol {protocol_version}"',
        "",
        "NS_ :",
        "    CM_",
        "    BA_DEF_",
        "    BA_DEF_DEF_",
        "    BA_",
        "    VAL_",
        "",
        "BS_:",
        "",
        "BU_: GUI MCU",
        "",
    ]

    for key, message in identifiers.items():
        frame_id = encoded_frame_id(message)
        message_name = dbc_name(key)
        transmitter, receiver = DIRECTIONS[message["direction"]]
        lines.append(
            f"BO_ {frame_id} {message_name}: {payload_size} {transmitter}"
        )

        for byte_index in range(payload_size):
            signal_name = f"PayloadByte{byte_index}"
            multiplex = ""
            if key == "gui_command" and byte_index == 0:
                signal_name = "CommandCode"
                multiplex = " M"
            lines.append(
                f" SG_ {signal_name}{multiplex} : {byte_index * 8}|8@1+ "
                f'(1,0) [0|255] "" {receiver}'
            )
        lines.append("")

    for key, message in identifiers.items():
        description = dbc_string(str(message.get("description", "")))
        if description:
            lines.append(
                f'CM_ BO_ {encoded_frame_id(message)} "{description}";'
            )
    lines.append("")

    lines.extend([
        'BA_DEF_ BO_ "VFrameFormat" ENUM "StandardCAN","ExtendedCAN";',
        'BA_DEF_DEF_ "VFrameFormat" "StandardCAN";',
    ])
    for message in identifiers.values():
        if message["type"] == "extended":
            lines.append(
                f'BA_ "VFrameFormat" BO_ {encoded_frame_id(message)} 1;'
            )
    lines.append("")

    gui_command = identifiers.get("gui_command")
    commands = definition.get("commands", {})
    if gui_command is not None and commands:
        values = " ".join(
            f'{command["code"]} "{dbc_string(dbc_name(key))}"'
            for key, command in commands.items()
        )
        lines.append(
            f"VAL_ {encoded_frame_id(gui_command)} CommandCode {values} ;"
        )
        lines.append("")

    return "\n".join(lines)


def generate_dbc(yaml_path: Path, output_path: Path) -> None:
    definition = load_definition(yaml_path)
    output_path.write_text(
        generate_dbc_text(definition),
        encoding="utf-8",
        newline="\n",
    )
    print(f"Successfully generated {output_path}")


def main(arguments: list[str]) -> int:
    if len(arguments) == 0:
        repository_root = Path(__file__).resolve().parents[1]
        yaml_path = repository_root / "protocol" / "can_protocol.yaml"
        output_path = repository_root / "protocol" / "can_gui.dbc"
    elif len(arguments) == 2:
        yaml_path = Path(arguments[0])
        output_path = Path(arguments[1])
    else:
        print("Usage: python generate_dbc.py [<input.yaml> <output.dbc>]")
        return 1

    try:
        generate_dbc(yaml_path, output_path)
    except (OSError, ValueError, yaml.YAMLError) as error:
        print(f"DBC generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
