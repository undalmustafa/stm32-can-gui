from pathlib import Path
import sys

import yaml


def generate_python_module(yaml_path, out_path):
    with open(yaml_path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)

    with open(out_path, "w", encoding="utf-8", newline="\n") as out:
        out.write("# AUTO-GENERATED from can_protocol.yaml — DO NOT EDIT\n\n")

        protocol = data.get("protocol", {})
        out.write(f"PROTOCOL_VERSION = {protocol.get('version', 1)}\n")
        out.write(f"PAYLOAD_SIZE = {protocol.get('payload_size', 8)}\n\n")

        out.write("# CAN Identifiers\n")
        identifiers = data.get("identifiers", {})
        for key, val in identifiers.items():
            name = key.upper()
            id_val = val["id"]
            if val["type"] == "extended":
                out.write(f"{name}_ID_EXT = 0x{id_val:08X}\n")
            else:
                out.write(f"{name}_TX_ID = 0x{id_val:03X}\n")
        out.write("\n")

        log_transport = data.get("log_transport", {})
        out.write("# Event Log Transport\n")
        for key in (
            "version", "record_size", "ram_capacity", "fragment_data_size",
            "info_wire_size", "info_fragment_count",
            "record_fragment_count",
        ):
            out.write(f"LOG_{key.upper()} = {log_transport[key]}\n")
        for key in (
            "record_magic", "commit_marker", "info_fragment_base",
            "record_fragment_base", "error_frame",
        ):
            out.write(
                f"LOG_{key.upper()} = 0x{log_transport[key]:X}\n"
            )
        for key, val in data.get("log_heartbeat_flags", {}).items():
            out.write(
                f"LOG_HEARTBEAT_FLAG_{key.upper()} = "
                f"0x{(1 << val['bit']):02X}\n"
            )
        for key, val in data.get("log_error_codes", {}).items():
            out.write(
                f"LOG_ERROR_{key.upper()} = 0x{val['code']:02X}\n"
            )
        out.write("\n")

        out.write("# Timing Service Codes\n")
        out.write("TIMING_SERVICE_NAMES = {\n")
        for key, val in data.get("timing_service_codes", {}).items():
            out.write(f"    {val['code']}: {key.upper()!r},\n")
        out.write("}\n\n")

        out.write("# Command Codes\n")
        commands = data.get("commands", {})
        for key, val in commands.items():
            name = key.upper()
            code = val["code"]
            out.write(f"CMD_{name} = 0x{code:02X}\n")
        out.write("\n")

        out.write("# Command Acknowledgement Status Codes\n")
        for key, val in data.get("command_ack_status_codes", {}).items():
            name = key.upper()
            code = val["code"]
            out.write(f"COMMAND_ACK_{name} = 0x{code:02X}\n")
        out.write("\n")

        out.write("# Command Acknowledgement Flags\n")
        for key, val in data.get("command_ack_flags", {}).items():
            name = key.upper()
            bit = val["bit"]
            out.write(f"COMMAND_ACK_FLAG_{name} = 0x{(1 << bit):02X}\n")
        out.write("\n")

        out.write("# Slot Flags\n")
        for key, val in data.get("slot_flags", {}).items():
            name = key.upper()
            bit = val["bit"]
            out.write(f"SLOT_FLAG_{name} = 0x{(1 << bit):02X}\n")
        out.write("\n")

        out.write("# System Status Control Policy\n")
        for section, prefix in (
            ("system_status_request_flags", "SYSTEM_REQUEST"),
            ("system_status_physical_flags", "SYSTEM_PHYSICAL"),
            ("system_status_override_flags", "SYSTEM_OVERRIDE"),
            ("pwm_status_control_flags", "PWM_CONTROL"),
            ("can_rx_health_flags", "CAN_RX_HEALTH"),
            ("timing_service_flags", "TIMING_SERVICE"),
        ):
            for key, val in data.get(section, {}).items():
                name = key.upper()
                bit = val["bit"]
                out.write(
                    f"{prefix}_{name} = 0x{(1 << bit):02X}\n"
                )
        out.write("\n")

        out.write("# PWM Self-Test State Codes\n")
        out.write("PWM_SELF_TEST_STATE_NAMES = {\n")
        for key, val in data.get("pwm_self_test_state_codes", {}).items():
            code = val["code"]
            display_name = val.get(
                "display_name", key.replace("_", " ").title()
            )
            out.write(f"    {code}: {display_name!r},\n")
        out.write("}\n\n")

        out.write("# Alarm Enable Flags\n")
        for key, val in data.get("alarm_enable_flags", {}).items():
            name = key.upper()
            bit = val["bit"]
            out.write(f"RTC_ALARM_ENABLE_{name} = 0x{(1 << bit):02X}\n")
        out.write("\n")

        out.write("# Alarm Event Flags\n")
        for key, val in data.get("alarm_event_flags", {}).items():
            name = key.upper()
            bit = val["bit"]
            out.write(f"RTC_ALARM_EVENT_{name} = 0x{(1 << bit):02X}\n")
        out.write("\n")

        tic12400_transport = data.get("tic12400_transport", {})
        out.write("# TIC12400 Telemetry\n")
        out.write(
            "TIC12400_CHANNEL_COUNT = "
            f"{tic12400_transport.get('channel_count', 24)}\n"
        )
        out.write(
            "TIC12400_BATTERY_CAPABLE_MASK = "
            f"0x{tic12400_transport.get(
                'battery_capable_mask', 0x3FF):06X}\n"
        )
        for key, val in data.get("tic12400_switch_flags", {}).items():
            name = key.upper()
            bit = val["bit"]
            out.write(
                f"TIC12400_SWITCH_{name} = 0x{(1 << bit):02X}\n"
            )
        for key, val in data.get("tic12400_profile_flags", {}).items():
            name = key.upper()
            bit = val["bit"]
            out.write(
                f"TIC12400_PROFILE_{name} = 0x{(1 << bit):02X}\n"
            )
        for key, val in data.get("tic12400_status_flags", {}).items():
            name = key.upper()
            bit = val["bit"]
            out.write(
                f"TIC12400_STATUS_{name} = 0x{(1 << bit):02X}\n"
            )
        for key, val in data.get(
                "tic12400_transaction_flags", {}).items():
            name = key.upper()
            bit = val["bit"]
            out.write(
                f"TIC12400_TRANSACTION_{name} = 0x{(1 << bit):02X}\n"
            )
        out.write("TIC12400_RESULT_NAMES = {\n")
        for key, val in data.get("tic12400_result_codes", {}).items():
            code = val["code"]
            out.write(f"    0x{code:02X}: {key.upper()!r},\n")
        out.write("}\n")
        out.write("\n")

        out.write("# RTC Status Definitions\n")
        out.write("RTC_STATUS_DEFINITIONS = {\n")
        rtc_status_codes = data.get("rtc_status_codes", {})
        for key, val in rtc_status_codes.items():
            code = val["code"]
            severity = val["severity"]
            severity_name = {
                "info": "INFO",
                "warning": "WARN",
                "fault": "FAULT",
            }[severity]
            mnemonic = val.get("mnemonic", key.upper())
            description = val.get("description", "")
            out.write(
                f"    0x{code:02X}: "
                f"({severity_name!r}, {mnemonic!r}, {description!r}),\n"
            )
        out.write("}\n\n")

        out.write("# RTC Communication Fault Codes\n")
        fault_codes = [
            f"0x{value['code']:02X}"
            for value in rtc_status_codes.values()
            if value.get("is_communication_fault")
        ]
        out.write(
            "RTC_COMMUNICATION_FAULT_CODES = {\n"
            f"    {', '.join(fault_codes)}\n"
            "}\n\n"
        )

    print(f"Successfully generated {out_path}")


if __name__ == "__main__":
    if len(sys.argv) == 1:
        repository_root = Path(__file__).resolve().parents[1]
        generate_python_module(
            repository_root / "protocol" / "can_protocol.yaml",
            repository_root / "python" / "can_protocol_generated.py",
        )
    elif len(sys.argv) == 3:
        generate_python_module(sys.argv[1], sys.argv[2])
    else:
        print(
            "Usage: python generate_python.py "
            "[<input.yaml> <output.py>]"
        )
        sys.exit(1)
