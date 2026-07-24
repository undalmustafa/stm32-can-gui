import yaml
import sys
import os
from datetime import datetime

def generate_python_module(yaml_path, out_path):
    with open(yaml_path, 'r') as f:
        data = yaml.safe_load(f)

    with open(out_path, 'w') as out:
        out.write(f"# AUTO-GENERATED from can_protocol.yaml — DO NOT EDIT\n")
        out.write(f"# Generated on: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")
        
        protocol = data.get('protocol', {})
        out.write(f"PROTOCOL_VERSION = {protocol.get('version', 1)}\n")
        out.write(f"PAYLOAD_SIZE = {protocol.get('payload_size', 8)}\n\n")

        out.write(f"# CAN Identifiers\n")
        identifiers = data.get('identifiers', {})
        for key, val in identifiers.items():
            name = key.upper()
            id_val = val['id']
            if val['type'] == 'extended':
                out.write(f"{name}_ID_EXT = 0x{id_val:08X}\n")
            else:
                out.write(f"{name}_TX_ID = 0x{id_val:03X}\n")
        out.write("\n")

        out.write(f"# Command Codes\n")
        commands = data.get('commands', {})
        for key, val in commands.items():
            name = key.upper()
            code = val['code']
            # Map rtc_set_time to CMD_RTC_SET_TIME, set_slot_1 to CMD_SET_SLOT_1, led_control to CMD_LED_CONTROL etc.
            out.write(f"CMD_{name} = 0x{code:02X}\n")
        out.write("\n")

        out.write(f"# Slot Flags\n")
        for key, val in data.get('slot_flags', {}).items():
            name = key.upper()
            bit = val['bit']
            out.write(f"SLOT_FLAG_{name} = 0x{(1<<bit):02X}\n")
        out.write("\n")

        out.write(f"# Alarm Enable Flags\n")
        for key, val in data.get('alarm_enable_flags', {}).items():
            name = key.upper()
            bit = val['bit']
            out.write(f"RTC_ALARM_ENABLE_{name} = 0x{(1<<bit):02X}\n")
        out.write("\n")

        out.write(f"# Alarm Event Flags\n")
        for key, val in data.get('alarm_event_flags', {}).items():
            name = key.upper()
            bit = val['bit']
            out.write(f"RTC_ALARM_EVENT_{name} = 0x{(1<<bit):02X}\n")
        out.write("\n")

        out.write(f"# RTC Status Definitions\n")
        out.write(f"RTC_STATUS_DEFINITIONS = {{\n")
        rtc_status_codes = data.get('rtc_status_codes', {})
        for key, val in rtc_status_codes.items():
            code = val['code']
            sev = val['severity']
            # Map severity to python logic: info->INFO, warning->WARN, fault->FAULT
            sev_str = "INFO" if sev == "info" else ("WARN" if sev == "warning" else "FAULT")
            mnemonic = val.get('mnemonic', key.upper())
            desc = val.get('description', '')
            out.write(f"    0x{code:02X}: (\"{sev_str}\", \"{mnemonic}\", \"{desc}\"),\n")
        out.write(f"}}\n\n")
        
        out.write(f"# RTC Communication Fault Codes\n")
        fault_codes = [f"0x{v['code']:02X}" for k, v in rtc_status_codes.items() if v.get('is_communication_fault')]
        out.write(f"RTC_COMMUNICATION_FAULT_CODES = {{\n    {', '.join(fault_codes)}\n}}\n\n")

    print(f"Successfully generated {out_path}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python generate_python.py <input.yaml> <output.py>")
        sys.exit(1)
    generate_python_module(sys.argv[1], sys.argv[2])
