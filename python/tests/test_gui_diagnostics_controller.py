import sys
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE_ROOT))

from can_gui_app.diagnostics_controller import (  # noqa: E402
    DiagnosticsController,
    decode_read_did_response,
)
from can_gui_app.protocol import (  # noqa: E402
    UDS_DID_PROTOCOL_INFO,
    UDS_DID_RESET_REASON,
    UDS_DID_RUNTIME_HEALTH,
    UDS_DID_STARTUP_HEALTH,
)
from can_gui_app.uds_client import UdsResult  # noqa: E402


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def u32(value):
    return int(value).to_bytes(4, "big")


def main():
    protocol_data = bytes([1, 2, 3, 2, 0, 0x07, 0xE0, 0x07, 0xE8])
    startup_data = (
        u32(0x0F) + u32(0x07) + u32(0x08) +
        u32(3) + u32(0x55) + bytes([1])
    )
    first_payload = (
        bytes([0x62, 0xF1, 0x00]) + protocol_data +
        bytes([0xF1, 0x01]) + startup_data
    )
    decoded = decode_read_did_response(first_payload)
    expect(decoded[UDS_DID_PROTOCOL_INFO]["isotp_capacity"] == 512,
           "protocol ISO-TP capacity is decoded big-endian")
    expect(decoded[UDS_DID_STARTUP_HEALTH]["degraded"],
           "startup degraded state is decoded")

    requests = []
    renders = []
    events = []
    connected = [True]

    def read_dids(dids, callback):
        requests.append((tuple(dids), callback))
        return True

    controller = DiagnosticsController(
        read_dids=read_dids,
        connected_provider=lambda: connected[0],
        renderer=lambda **state: renders.append(state),
        event_writer=lambda **event: events.append(event),
    )
    expect(controller.poll(), "the first diagnostic batch is requested")
    expect(requests[-1][0] == (
        UDS_DID_PROTOCOL_INFO, UDS_DID_STARTUP_HEALTH
    ), "static and startup diagnostics share the first request")
    requests[-1][1](UdsResult(True, 0x22, payload=first_payload))
    expect(controller.status == "OK" and
           UDS_DID_STARTUP_HEALTH in controller.values,
           "a positive response updates live diagnostic state")

    runtime_values = [12345, 1, 2, 3, 4, 5, 6]
    runtime_data = b"".join(u32(value) for value in runtime_values)
    reset_data = u32(0x0A) + u32(0x12345678) + u32(1)
    second_payload = (
        bytes([0x62, 0xF1, 0x02]) + runtime_data +
        bytes([0xF1, 0x03]) + reset_data
    )
    expect(controller.poll(), "the runtime diagnostic batch is requested")
    expect(requests[-1][0] == (
        UDS_DID_RUNTIME_HEALTH, UDS_DID_RESET_REASON
    ), "runtime and reset evidence share the second request")
    requests[-1][1](UdsResult(True, 0x22, payload=second_payload))
    expect(controller.values[UDS_DID_RUNTIME_HEALTH]["uptime_ms"] == 12345,
           "runtime counters are decoded big-endian")
    expect(controller.values[UDS_DID_RESET_REASON]["raw_rsr"] == 0x12345678,
           "raw reset evidence remains visible")

    controller.poll()
    requests[-1][1](UdsResult(
        False, 0x22, error_code="TIMEOUT", detail="no response"
    ))
    event_count = len(events)
    controller.poll()
    requests[-1][1](UdsResult(
        False, 0x22, error_code="TIMEOUT", detail="no response"
    ))
    expect(len(events) == event_count,
           "repeated communication faults do not flood the event log")

    connected[0] = False
    controller.poll()
    expect(controller.status == "DISCONNECTED",
           "connection loss is rendered without starting a request")
    expect(renders[-1]["status"] == "DISCONNECTED",
           "the presentation receives connection state")

    print("PASS: UDS DID decoding and diagnostic polling controller")


if __name__ == "__main__":
    main()
