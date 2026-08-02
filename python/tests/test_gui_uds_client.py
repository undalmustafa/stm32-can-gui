import sys
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE_ROOT))

from can_gui_app.isotp_client import IsoTpClient  # noqa: E402
from can_gui_app.uds_client import UdsClient  # noqa: E402
from can_gui_app.protocol import (  # noqa: E402
    UDS_DID_PROTOCOL_INFO,
    UDS_ROUTINE_ERASE_INACTIVE_SLOT,
    UDS_SERVICE_REQUEST_DOWNLOAD,
    UDS_SERVICE_READ_DATA_BY_IDENTIFIER,
)


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    clock = [10.0]
    frames = []
    transport = IsoTpClient(
        lambda frame: frames.append(bytes(frame)) or True,
        clock=lambda: clock[0],
    )
    expect(transport.start([0x22, 0xF1, 0x00]),
           "a short UDS request starts ISO-TP")
    expect(frames[0] == bytes([3, 0x22, 0xF1, 0x00, 0, 0, 0, 0]),
           "the request is encoded as a padded single frame")

    response = bytes([0x62, 0xF1, 0x00, 1, 2, 3, 2, 0, 0x07, 0xE0, 0x07, 0xE8])
    transport.handle_frame(bytes([0x10, len(response)]) + response[:6])
    expect(frames[-1] == bytes([0x30, 0, 0, 0, 0, 0, 0, 0]),
           "a first frame is acknowledged with flow-control CTS")
    transport.handle_frame(bytes([0x21]) + response[6:] + bytes(1))
    result = transport.take_result()
    expect(result.ok and result.payload == response,
           "consecutive frames are reassembled without padding")

    expect(transport.start([0x3E, 0]), "another request can start")
    clock[0] += 1.1
    transport.process()
    result = transport.take_result()
    expect(not result.ok and result.error_code == "TIMEOUT",
           "an absent response terminates with a bounded timeout")

    expect(transport.start([0x22, 0xF1, 0x00]),
           "sequence mismatch test starts")
    transport.handle_frame(bytes([0x10, 9, 0x62, 0xF1, 0, 1, 2, 3]))
    transport.handle_frame(bytes([0x22, 4, 5, 6, 0, 0, 0, 0]))
    result = transport.take_result()
    expect(not result.ok and result.error_code == "SEQUENCE_MISMATCH",
           "out-of-order consecutive frames fail closed")

    expect(transport.start([0x3E, 0]),
           "Classic CAN DLC validation test starts")
    transport.handle_frame(bytes([2, 0x7E, 0]) + bytes(9))
    result = transport.take_result()
    expect(not result.ok and result.error_code == "INVALID_FRAME",
           "CAN FD-sized diagnostic responses fail closed")

    long_request = bytes(range(1, 25))
    expect(transport.start(long_request),
           "a multi-frame diagnostic request starts")
    expect(frames[-1] == bytes([0x10, 24]) + long_request[:6],
           "the request first frame carries its 12-bit payload length")
    transport.handle_frame(bytes([0x30, 2, 0, 0, 0, 0, 0, 0]))
    expect(frames[-2][0] == 0x21 and frames[-1][0] == 0x22,
           "receiver block size bounds consecutive-frame bursts")
    transport.handle_frame(bytes([0x30, 0, 0, 0, 0, 0, 0, 0]))
    expect(frames[-1][0] == 0x23,
           "a subsequent CTS completes the request")
    transport.handle_frame(bytes([2, 0x76, 1, 0, 0, 0, 0, 0]))
    result = transport.take_result()
    expect(result.ok and result.payload == bytes([0x76, 1]),
           "the response is accepted after multi-frame transmission")

    timed_frames = []
    timed_transport = IsoTpClient(
        lambda frame: timed_frames.append(bytes(frame)) or True,
        clock=lambda: clock[0],
    )
    expect(timed_transport.start(bytes(range(20))),
           "STmin timing test starts")
    timed_transport.handle_frame(bytes([0x30, 0, 5, 0, 0, 0, 0, 0]))
    expect(len(timed_frames) == 2,
           "only one consecutive frame is sent before STmin elapses")
    timed_transport.process()
    expect(len(timed_frames) == 2,
           "polling early does not violate receiver STmin")
    clock[0] += 0.005
    timed_transport.process()
    expect(len(timed_frames) == 3,
           "polling at the deadline sends the next frame")
    timed_transport.handle_frame(bytes([1, 0x76, 0, 0, 0, 0, 0, 0]))
    timed_transport.take_result()

    wait_transport = IsoTpClient(
        lambda _frame: True,
        clock=lambda: clock[0],
        max_flow_control_wait=1,
    )
    expect(wait_transport.start(bytes(range(8))),
           "flow-control WAIT limit test starts")
    wait_transport.handle_frame(bytes([0x31, 0, 0, 0, 0, 0, 0, 0]))
    wait_transport.handle_frame(bytes([0x31, 0, 0, 0, 0, 0, 0, 0]))
    result = wait_transport.take_result()
    expect(not result.ok and result.error_code == "WAIT_LIMIT_EXCEEDED",
           "unbounded flow-control WAIT is rejected")

    uds_frames = []
    callbacks = []
    client = UdsClient(
        lambda frame: uds_frames.append(bytes(frame)) or True,
        clock=lambda: clock[0],
    )
    expect(client.read_dids([UDS_DID_PROTOCOL_INFO], callbacks.append),
           "ReadDataByIdentifier is queued")
    expect(uds_frames[-1][0:4] == bytes([3, 0x22, 0xF1, 0x00]),
           "UDS DID request uses the generated identifier")
    client.handle_frame(bytes([4, 0x62, 0xF1, 0x00, 0xAA, 0, 0, 0]))
    expect(callbacks[-1].ok and callbacks[-1].service ==
           UDS_SERVICE_READ_DATA_BY_IDENTIFIER,
           "a matching positive response reaches its callback")

    client.read_dids([UDS_DID_PROTOCOL_INFO], callbacks.append)
    client.handle_frame(bytes([3, 0x7F, 0x22, 0x31, 0, 0, 0, 0]))
    expect(not callbacks[-1].ok and callbacks[-1].nrc == 0x31,
           "UDS negative response codes remain observable")

    try:
        client.read_dids([0xF100, 0xF101, 0xF102, 0xF103])
    except ValueError:
        pass
    else:
        raise AssertionError("oversized Classic CAN requests are rejected")

    download_callbacks = []
    expect(client.routine_control(
        1, UDS_ROUTINE_ERASE_INACTIVE_SLOT, download_callbacks.append
    ), "RoutineControl request is queued")
    client.handle_frame(bytes([
        4, 0x71, 1, 0xFF, 0x00, 0, 0, 0
    ]))
    expect(download_callbacks[-1].ok,
           "RoutineControl positive response reaches the caller")

    expect(client.request_download(
        0x08100000, 1040, download_callbacks.append
    ), "32-bit RequestDownload is queued")
    expect(uds_frames[-1][:3] == bytes([0x10, 11, 0x34]),
           "RequestDownload uses multi-frame ISO-TP")
    client.handle_frame(bytes([0x30, 0, 0, 0, 0, 0, 0, 0]))
    expect(uds_frames[-1][0] == 0x21,
           "RequestDownload remainder uses a consecutive frame")
    client.handle_frame(bytes([4, 0x74, 0x20, 0x01, 0x02, 0, 0, 0]))
    expect(download_callbacks[-1].ok and
           download_callbacks[-1].service == UDS_SERVICE_REQUEST_DOWNLOAD,
           "RequestDownload response is parsed by the shared UDS queue")

    try:
        client.transfer_data(1, bytes(257))
    except ValueError:
        pass
    else:
        raise AssertionError("oversized firmware block is rejected locally")

    print("PASS: non-blocking Python ISO-TP and UDS client")


if __name__ == "__main__":
    main()
