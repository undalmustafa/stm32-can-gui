from pathlib import Path
import sys

import yaml


def generate_c_header(yaml_path, out_path):
    with open(yaml_path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)

    with open(out_path, "w", encoding="utf-8", newline="\n") as out:
        out.write("/* AUTO-GENERATED from can_protocol.yaml — DO NOT EDIT */\n\n")
        out.write("#ifndef CAN_PROTOCOL_GENERATED_H\n")
        out.write("#define CAN_PROTOCOL_GENERATED_H\n\n")

        protocol = data.get("protocol", {})
        out.write("/* Protocol Version */\n")
        out.write(
            f"#define CAN_PROTOCOL_VERSION "
            f"{protocol.get('version', 1)}U\n"
        )
        out.write(
            f"#define CAN_PROTOCOL_PAYLOAD_SIZE "
            f"{protocol.get('payload_size', 8)}U\n\n"
        )

        out.write("/* CAN Identifiers */\n")
        identifiers = data.get("identifiers", {})
        for key, val in identifiers.items():
            name = key.upper()
            id_val = val["id"]
            if val["type"] == "extended":
                out.write(
                    f"#define CAN_PROTOCOL_{name}_ID_EXT "
                    f"0x{id_val:08X}UL\n"
                )
            else:
                out.write(
                    f"#define CAN_PROTOCOL_{name}_TX_ID "
                    f"0x{id_val:03X}U\n"
                )
        out.write("\n")

        out.write("/* Slot Flags */\n")
        for key, val in data.get("slot_flags", {}).items():
            name = key.upper()
            bit = val["bit"]
            out.write(
                f"#define CAN_PROTOCOL_SLOT_FLAG_{name} "
                f"0x{(1 << bit):02X}U\n"
            )
        out.write("\n")

        out.write("/* Alarm Enable Flags */\n")
        mask = 0
        for key, val in data.get("alarm_enable_flags", {}).items():
            name = key.upper()
            bit = val["bit"]
            mask |= (1 << bit)
            out.write(
                f"#define CAN_PROTOCOL_RTC_ALARM_ENABLE_{name} "
                f"0x{(1 << bit):02X}U\n"
            )
        out.write(
            f"#define CAN_PROTOCOL_RTC_ALARM_ENABLE_MASK "
            f"0x{mask:02X}U\n\n"
        )

        out.write("/* Alarm Event Flags */\n")
        for key, val in data.get("alarm_event_flags", {}).items():
            name = key.upper()
            bit = val["bit"]
            out.write(
                f"#define CAN_PROTOCOL_RTC_ALARM_EVENT_{name} "
                f"0x{(1 << bit):02X}U\n"
            )
        out.write("\n")

        out.write("/* Command Codes */\n")
        out.write("typedef enum\n{\n")
        commands = data.get("commands", {})
        lines = []
        for key, val in commands.items():
            name = key.upper()
            code = val["code"]
            lines.append(f"    CAN_PROTOCOL_CMD_{name} = 0x{code:02X}U")
        out.write(",\n".join(lines))
        out.write("\n} CAN_Protocol_Command_t;\n\n")

        out.write("/* RTC Status Codes */\n")
        out.write("typedef enum\n{\n")
        rtc_status_codes = data.get("rtc_status_codes", {})
        lines = []
        for key, val in rtc_status_codes.items():
            name = key.upper()
            code = val["code"]
            lines.append(
                f"    CAN_PROTOCOL_RTC_STATUS_{name} = 0x{code:02X}U"
            )
        out.write(",\n".join(lines))
        out.write("\n} CAN_Protocol_RtcStatusCode_t;\n\n")

        out.write("/* RTC Alarm Event Codes */\n")
        out.write("typedef enum\n{\n")
        rtc_alarm_event_codes = data.get("rtc_alarm_event_codes", {})
        lines = []
        for key, val in rtc_alarm_event_codes.items():
            name = key.upper()
            code = val["code"]
            lines.append(
                f"    CAN_PROTOCOL_RTC_ALARM_EVENT_{name} = 0x{code:02X}U"
            )
        out.write(",\n".join(lines))
        out.write("\n} CAN_Protocol_RtcAlarmEventCode_t;\n\n")

        out.write("#endif /* CAN_PROTOCOL_GENERATED_H */\n")
    print(f"Successfully generated {out_path}")


if __name__ == "__main__":
    if len(sys.argv) == 1:
        repository_root = Path(__file__).resolve().parents[1]
        generate_c_header(
            repository_root / "protocol" / "can_protocol.yaml",
            repository_root / "Core" / "Inc" /
            "can_protocol_generated.h",
        )
    elif len(sys.argv) == 3:
        generate_c_header(sys.argv[1], sys.argv[2])
    else:
        print("Usage: python generate_c.py [<input.yaml> <output.h>]")
        sys.exit(1)
