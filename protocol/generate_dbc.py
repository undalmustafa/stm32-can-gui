#!/usr/bin/env python3
"""Generate a deterministic CAN database from can_protocol.yaml."""

import math
import re
import sys
from pathlib import Path

import yaml


DBC_EXTENDED_ID_FLAG = 0x80000000
DBC_NAME_PATTERN = re.compile(r"[^A-Za-z0-9_]")
DBC_SIGNAL_NAME_PATTERN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
DIRECTIONS = {
    "gui_to_mcu": ("GUI", "MCU"),
    "mcu_to_gui": ("MCU", "GUI"),
}


def dbc_number(value: int | float) -> str:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError("DBC numeric value must be an integer or float")
    if isinstance(value, int):
        return str(value)
    if not math.isfinite(value):
        raise ValueError("DBC numeric value must be finite")
    return format(value, ".15g")


def signal_value_table(definition: dict, source: str) -> list[tuple[int, str]]:
    table = definition.get(source)
    if not isinstance(table, dict) or not table:
        raise ValueError(f"DBC value source {source} must be a non-empty mapping")

    values = []
    for key, entry in table.items():
        if isinstance(entry, dict):
            raw_value = entry.get("code")
            label = entry.get("mnemonic", dbc_name(str(key)))
        else:
            raw_value = key
            label = entry
        if not isinstance(raw_value, int) or isinstance(raw_value, bool):
            raise ValueError(f"DBC value source {source} has a non-integer value")
        if not isinstance(label, str) or not label:
            raise ValueError(f"DBC value source {source} has an invalid label")
        values.append((raw_value, label))
    return values


def validate_signals(definition: dict, message_key: str, payload_size: int) -> None:
    signal_map = definition.get("dbc_signals", {})
    signals = signal_map.get(message_key)
    if signals is None:
        return
    if not isinstance(signals, list) or not signals:
        raise ValueError(f"DBC signals for {message_key} must be a non-empty list")

    occupied_bits = set()
    signal_names = set()
    payload_bits = payload_size * 8
    for signal in signals:
        if not isinstance(signal, dict):
            raise ValueError(f"DBC signal in {message_key} must be a mapping")
        name = signal.get("name")
        start_bit = signal.get("start_bit")
        length = signal.get("length")
        signed = signal.get("signed", False)
        factor = signal.get("factor", 1)
        offset = signal.get("offset", 0)
        minimum = signal.get("minimum")
        maximum = signal.get("maximum")

        if (not isinstance(name, str) or
                DBC_SIGNAL_NAME_PATTERN.fullmatch(name) is None):
            raise ValueError(f"DBC signal in {message_key} has an invalid name")
        if name in signal_names:
            raise ValueError(f"DBC signal {message_key}.{name} is duplicated")
        signal_names.add(name)
        if (not isinstance(start_bit, int) or isinstance(start_bit, bool) or
                not isinstance(length, int) or isinstance(length, bool) or
                start_bit < 0 or length < 1 or start_bit + length > payload_bits):
            raise ValueError(f"DBC signal {message_key}.{name} is outside the frame")
        if not isinstance(signed, bool):
            raise ValueError(
                f"DBC signal {message_key}.{name} signed must be boolean"
            )
        multiplexor = signal.get("multiplexor", False)
        if not isinstance(multiplexor, bool):
            raise ValueError(
                f"DBC signal {message_key}.{name} multiplexor must be boolean"
            )
        dbc_number(factor)
        dbc_number(offset)
        if factor <= 0:
            raise ValueError(
                f"DBC signal {message_key}.{name} factor must be positive"
            )

        raw_minimum = -(1 << (length - 1)) if signed else 0
        raw_maximum = (1 << (length - (1 if signed else 0))) - 1
        if minimum is None:
            minimum = raw_minimum * factor + offset
        if maximum is None:
            maximum = raw_maximum * factor + offset
        dbc_number(minimum)
        dbc_number(maximum)
        if minimum > maximum:
            raise ValueError(f"DBC signal {message_key}.{name} range is inverted")
        physical_minimum = raw_minimum * factor + offset
        physical_maximum = raw_maximum * factor + offset
        if minimum < physical_minimum or maximum > physical_maximum:
            raise ValueError(
                f"DBC signal {message_key}.{name} range is not representable"
            )

        bits = set(range(start_bit, start_bit + length))
        if bits & occupied_bits:
            raise ValueError(f"DBC signal {message_key}.{name} overlaps another signal")
        occupied_bits.update(bits)

        source = signal.get("value_source")
        if source is not None:
            if not isinstance(source, str):
                raise ValueError(
                    f"DBC signal {message_key}.{name} value source is invalid"
                )
            for raw_value, _label in signal_value_table(definition, source):
                if not raw_minimum <= raw_value <= raw_maximum:
                    raise ValueError(
                        f"DBC value {raw_value} does not fit {message_key}.{name}"
                    )

    if len(occupied_bits) != payload_bits:
        raise ValueError(f"DBC signals for {message_key} do not cover the frame")


def dbc_name(value: str) -> str:
    name = DBC_NAME_PATTERN.sub("_", value)
    if not name or name[0].isdigit():
        name = f"CAN_{name}"
    return name.upper()


def dbc_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def validate_log_transport(definition: dict, payload_size: int) -> None:
    transport = definition.get("log_transport")
    if not isinstance(transport, dict):
        raise ValueError("log_transport section must be a mapping")

    fields = (
        "version", "record_size", "ram_capacity", "record_magic",
        "commit_marker", "fragment_data_size", "info_wire_size",
        "info_fragment_base", "info_fragment_count",
        "record_fragment_base", "record_fragment_count", "error_frame",
    )
    for field in fields:
        value = transport.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < 1:
            raise ValueError(f"log_transport {field} must be a positive integer")

    fragment_size = transport["fragment_data_size"]
    if fragment_size != payload_size - 1:
        raise ValueError("log fragment data must fill the CAN payload")
    for kind, wire_size_key, count_key in (
        ("info", "info_wire_size", "info_fragment_count"),
        ("record", "record_size", "record_fragment_count"),
    ):
        wire_size = transport[wire_size_key]
        expected_count = (wire_size + fragment_size - 1) // fragment_size
        if transport[count_key] != expected_count:
            raise ValueError(f"log {kind} fragment count does not fit wire size")

    info_headers = set(range(
        transport["info_fragment_base"],
        transport["info_fragment_base"] + transport["info_fragment_count"],
    ))
    record_headers = set(range(
        transport["record_fragment_base"],
        transport["record_fragment_base"] +
        transport["record_fragment_count"],
    ))
    error_header = transport["error_frame"]
    if (max(info_headers | record_headers | {error_header}) > 0xFF or
            info_headers & record_headers or
            error_header in info_headers or error_header in record_headers):
        raise ValueError("log response headers overlap or exceed one byte")
    if (transport["version"] > 0xFF or
            transport["ram_capacity"] > 0xFF or
            transport["record_magic"] > 0xFFFFFFFF or
            transport["commit_marker"] > 0xFFFF):
        raise ValueError("log transport field exceeds its wire representation")


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
    validate_log_transport(definition, payload_size)
    signal_map = definition.get("dbc_signals", {})
    if not isinstance(signal_map, dict):
        raise ValueError("dbc_signals section must be a mapping")
    unknown_signal_messages = set(signal_map) - set(identifiers)
    if unknown_signal_messages:
        unknown = sorted(unknown_signal_messages)[0]
        raise ValueError(f"DBC signals reference unknown identifier {unknown}")

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
        validate_signals(definition, key, payload_size)

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

    timing_codes = sorted(
        value for value, _label in signal_value_table(
            definition, "timing_service_codes"
        )
    )
    if timing_codes != list(range(len(timing_codes))):
        raise ValueError("timing service codes must be contiguous from zero")


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
    signal_map = definition.get("dbc_signals", {})

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

        signals = signal_map.get(key)
        if signals is None:
            signals = [
                {
                    "name": ("CommandCode" if key == "gui_command" and
                             byte_index == 0 else f"PayloadByte{byte_index}"),
                    "start_bit": byte_index * 8,
                    "length": 8,
                    "multiplexor": key == "gui_command" and byte_index == 0,
                    "minimum": 0,
                    "maximum": 255,
                }
                for byte_index in range(payload_size)
            ]

        for signal in signals:
            length = signal["length"]
            signed = signal.get("signed", False)
            factor = signal.get("factor", 1)
            offset = signal.get("offset", 0)
            raw_minimum = -(1 << (length - 1)) if signed else 0
            minimum = signal.get("minimum")
            maximum = signal.get("maximum")
            if minimum is None:
                minimum = raw_minimum * factor + offset
            if maximum is None:
                raw_maximum = (1 << (length - (1 if signed else 0))) - 1
                maximum = raw_maximum * factor + offset
            multiplex = " M" if signal.get("multiplexor", False) else ""
            sign = "-" if signed else "+"
            unit = dbc_string(str(signal.get("unit", "")))
            lines.append(
                f' SG_ {signal["name"]}{multiplex} : '
                f'{signal["start_bit"]}|{length}@1{sign} '
                f'({dbc_number(factor)},{dbc_number(offset)}) '
                f'[{dbc_number(minimum)}|{dbc_number(maximum)}] '
                f'"{unit}" {receiver}'
            )
        lines.append("")

    for key, message in identifiers.items():
        description = dbc_string(str(message.get("description", "")))
        if description:
            lines.append(
                f'CM_ BO_ {encoded_frame_id(message)} "{description}";'
            )
        for signal in signal_map.get(key, []):
            signal_description = signal.get("description")
            if signal_description:
                lines.append(
                    f'CM_ SG_ {encoded_frame_id(message)} {signal["name"]} '
                    f'"{dbc_string(str(signal_description))}";'
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

    for key, signals in signal_map.items():
        frame_id = encoded_frame_id(identifiers[key])
        for signal in signals:
            source = signal.get("value_source")
            if source is None:
                continue
            values = " ".join(
                f'{raw_value} "{dbc_string(label)}"'
                for raw_value, label in signal_value_table(definition, source)
            )
            lines.append(f'VAL_ {frame_id} {signal["name"]} {values} ;')
    if signal_map:
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
