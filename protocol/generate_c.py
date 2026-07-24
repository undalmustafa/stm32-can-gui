import yaml
import sys
import os
from datetime import datetime

def generate_c_header(yaml_path, out_path):
    with open(yaml_path, 'r') as f:
        data = yaml.safe_load(f)

    with open(out_path, 'w') as out:
        out.write(f"/* AUTO-GENERATED from can_protocol.yaml — DO NOT EDIT */\n")
        out.write(f"/* Generated on: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')} */\n\n")
        out.write(f"#ifndef CAN_PROTOCOL_GENERATED_H\n")
        out.write(f"#define CAN_PROTOCOL_GENERATED_H\n\n")
        
        protocol = data.get('protocol', {})
        out.write(f"/* Protocol Version */\n")
        out.write(f"#define CAN_PROTOCOL_VERSION {protocol.get('version', 1)}U\n")
        out.write(f"#define CAN_PROTOCOL_PAYLOAD_SIZE {protocol.get('payload_size', 8)}U\n\n")

        out.write(f"/* CAN Identifiers */\n")
        identifiers = data.get('identifiers', {})
        for key, val in identifiers.items():
            name = key.upper()
            id_val = val['id']
            if val['type'] == 'extended':
                out.write(f"#define CAN_PROTOCOL_{name}_ID_EXT 0x{id_val:08X}UL\n")
            else:
                out.write(f"#define CAN_PROTOCOL_{name}_TX_ID 0x{id_val:03X}U\n")
        out.write("\n")

        out.write(f"/* Slot Flags */\n")
        for key, val in data.get('slot_flags', {}).items():
            name = key.upper()
            bit = val['bit']
            out.write(f"#define CAN_PROTOCOL_SLOT_FLAG_{name} 0x{(1<<bit):02X}U\n")
        out.write("\n")

        out.write(f"/* Alarm Enable Flags */\n")
        mask = 0
        for key, val in data.get('alarm_enable_flags', {}).items():
            name = key.upper()
            bit = val['bit']
            mask |= (1 << bit)
            out.write(f"#define CAN_PROTOCOL_RTC_ALARM_ENABLE_{name} 0x{(1<<bit):02X}U\n")
        out.write(f"#define CAN_PROTOCOL_RTC_ALARM_ENABLE_MASK 0x{mask:02X}U\n\n")

        out.write(f"/* Alarm Event Flags */\n")
        for key, val in data.get('alarm_event_flags', {}).items():
            name = key.upper()
            bit = val['bit']
            out.write(f"#define CAN_PROTOCOL_RTC_ALARM_EVENT_{name} 0x{(1<<bit):02X}U\n")
        out.write("\n")

        out.write(f"/* Command Codes */\n")
        out.write(f"typedef enum\n{{\n")
        commands = data.get('commands', {})
        lines = []
        for key, val in commands.items():
            name = key.upper()
            code = val['code']
            lines.append(f"    CAN_PROTOCOL_CMD_{name} = 0x{code:02X}U")
        out.write(",\n".join(lines))
        out.write(f"\n}} CAN_Protocol_Command_t;\n\n")

        out.write(f"/* RTC Status Codes */\n")
        out.write(f"typedef enum\n{{\n")
        rtc_status_codes = data.get('rtc_status_codes', {})
        lines = []
        for key, val in rtc_status_codes.items():
            name = key.upper()
            code = val['code']
            lines.append(f"    CAN_PROTOCOL_RTC_STATUS_{name} = 0x{code:02X}U")
        out.write(",\n".join(lines))
        out.write(f"\n}} CAN_Protocol_RtcStatusCode_t;\n\n")
        
        out.write(f"/* RTC Alarm Event Codes */\n")
        out.write(f"typedef enum\n{{\n")
        rtc_alarm_event_codes = data.get('rtc_alarm_event_codes', {})
        lines = []
        for key, val in rtc_alarm_event_codes.items():
            name = key.upper()
            code = val['code']
            lines.append(f"    CAN_PROTOCOL_RTC_ALARM_EVENT_{name} = 0x{code:02X}U")
        out.write(",\n".join(lines))
        out.write(f"\n}} CAN_Protocol_RtcAlarmEventCode_t;\n\n")

        out.write(f"#endif /* CAN_PROTOCOL_GENERATED_H */\n")
    print(f"Successfully generated {out_path}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python generate_c.py <input.yaml> <output.h>")
        sys.exit(1)
    generate_c_header(sys.argv[1], sys.argv[2])
