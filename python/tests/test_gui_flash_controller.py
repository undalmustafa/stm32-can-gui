import sys
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE_ROOT))

from can_gui_app.flash_controller import FlashController  # noqa: E402
from can_gui_app.protocol import (  # noqa: E402
    UDS_ROUTINE_ERASE_INACTIVE_SLOT,
    UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL,
    UDS_SERVICE_REQUEST_DOWNLOAD,
    UDS_SERVICE_REQUEST_TRANSFER_EXIT,
    UDS_SERVICE_ROUTINE_CONTROL,
    UDS_SERVICE_TRANSFER_DATA,
    UDS_SESSION_DEFAULT,
    UDS_SESSION_PROGRAMMING,
)
from can_gui_app.uds_client import UdsResult  # noqa: E402


SLOT_B_ADDRESS = 0x08100000
SLOT_SIZE = 0x000E0000


def expect(condition, description):
    if not condition:
        raise AssertionError(description)


def make_artifact(vector_address=SLOT_B_ADDRESS + 1024):
    artifact = bytearray([0xFF] * 1040)
    artifact[0:4] = (0x31474D49).to_bytes(4, "little")
    artifact[4:6] = (1).to_bytes(2, "little")
    artifact[6:8] = (1024).to_bytes(2, "little")
    artifact[8:12] = (16).to_bytes(4, "little")
    artifact[12:16] = vector_address.to_bytes(4, "little")
    artifact[16:20] = (vector_address + 1).to_bytes(4, "little")
    artifact[20:24] = (7).to_bytes(4, "little")
    artifact[24:28] = (42).to_bytes(4, "little")
    artifact[32:64] = bytes(range(1, 33))
    artifact[64:128] = bytes(range(64))
    artifact[1024:1040] = bytes(range(16))
    return bytes(artifact)


class TransportFixture:
    def __init__(self):
        self.requests = []

    def _queue(self, name, arguments, callback):
        self.requests.append((name, arguments, callback))
        return True

    def change_session(self, session, callback):
        return self._queue("session", (session,), callback)

    def routine_control(self, subfunction, routine, callback):
        return self._queue("routine", (subfunction, routine), callback)

    def request_download(self, address, size, callback):
        return self._queue("download", (address, size), callback)

    def transfer_data(self, sequence, data, callback):
        return self._queue("transfer", (sequence, bytes(data)), callback)

    def request_transfer_exit(self, callback):
        return self._queue("exit", (), callback)

    def respond(self, result):
        _name, _arguments, callback = self.requests.pop(0)
        callback(result)


def positive(service, payload=b""):
    return UdsResult(
        True,
        service,
        payload=bytes([(service + 0x40) & 0xFF]) + bytes(payload),
    )


def erase_payload(subfunction, status, address=SLOT_B_ADDRESS):
    return (
        bytes([subfunction]) +
        UDS_ROUTINE_ERASE_INACTIVE_SLOT.to_bytes(2, "big") +
        bytes([status, 2]) +
        address.to_bytes(4, "big") +
        SLOT_SIZE.to_bytes(4, "big")
    )


def make_controller(fixture, artifact):
    renders = []
    events = []
    controller = FlashController(
        change_session=fixture.change_session,
        routine_control=fixture.routine_control,
        request_download=fixture.request_download,
        transfer_data=fixture.transfer_data,
        request_transfer_exit=fixture.request_transfer_exit,
        connected_provider=lambda: True,
        renderer=lambda **state: renders.append(state),
        event_writer=lambda **event: events.append(event),
        file_loader=lambda _path: artifact,
    )
    return controller, renders, events


def test_complete_update():
    artifact = make_artifact()
    transport = TransportFixture()
    controller, renders, events = make_controller(transport, artifact)

    expect(controller.start("release-slot-b.img"),
           "valid signed artifact starts the workflow")
    expect(transport.requests[0][0:2] == (
        "session", (UDS_SESSION_PROGRAMMING,)
    ), "programming session is always first")
    transport.respond(positive(
        UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL,
        bytes([UDS_SESSION_PROGRAMMING, 0, 50, 1, 0xF4]),
    ))
    expect(transport.requests[0][0] == "routine" and
           transport.requests[0][1] == (
               1, UDS_ROUTINE_ERASE_INACTIVE_SLOT
           ), "inactive-slot erase starts before RequestDownload")

    transport.respond(positive(
        UDS_SERVICE_ROUTINE_CONTROL, erase_payload(1, 1)
    ))
    expect(transport.requests[0][1][0] == 3,
           "pending erase is polled through RequestRoutineResults")
    transport.respond(positive(
        UDS_SERVICE_ROUTINE_CONTROL, erase_payload(3, 0)
    ))
    expect(transport.requests[0][0:2] == (
        "download", (SLOT_B_ADDRESS, len(artifact))
    ), "ECU-selected slot address and exact artifact length are negotiated")

    transport.respond(positive(
        UDS_SERVICE_REQUEST_DOWNLOAD, bytes([0x20, 0x01, 0x02])
    ))
    block_count = 0
    retried = False
    while transport.requests[0][0] == "transfer":
        _name, (sequence, data), _callback = transport.requests[0]
        if not retried:
            first_block = (sequence, data)
            transport.respond(UdsResult(
                False,
                UDS_SERVICE_TRANSFER_DATA,
                error_code="TIMEOUT",
                detail="lost block response",
            ))
            expect(transport.requests[0][1] == first_block,
                   "lost TransferData ACK retries the identical block once")
            retried = True
            continue
        block_count += 1
        expect(1 <= len(data) <= 256 and len(data) % 16 == 0,
               "every GUI transfer block is flashword aligned")
        transport.respond(positive(
            UDS_SERVICE_TRANSFER_DATA, bytes([sequence])
        ))
    expect(block_count == 5,
           "1040-byte artifact is streamed without queueing the whole image")
    expect(transport.requests[0][0] == "exit",
           "signature verification is requested after all blocks")

    transport.respond(positive(UDS_SERVICE_REQUEST_TRANSFER_EXIT))
    expect(transport.requests[0][0:2] == (
        "session", (UDS_SESSION_DEFAULT,)
    ), "successful transfer closes programming session")
    transport.respond(positive(
        UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL,
        bytes([UDS_SESSION_DEFAULT, 0, 50, 1, 0xF4]),
    ))
    expect(controller.status == "COMPLETE" and not controller.busy,
           "accepted image reaches a terminal complete state")
    expect(renders[-1]["progress"] == 100,
           "operator progress reaches 100 percent only after acceptance")
    expect(events[-1]["event_code"] == "UPDATE_COMPLETE",
           "completed update is recorded in the product event log")


def test_target_mismatch_fails_before_download():
    transport = TransportFixture()
    controller, _renders, events = make_controller(
        transport, make_artifact(vector_address=0x08020400)
    )
    controller.start("wrong-slot.img")
    transport.respond(positive(
        UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL,
        bytes([UDS_SESSION_PROGRAMMING]),
    ))
    transport.respond(positive(
        UDS_SERVICE_ROUTINE_CONTROL, erase_payload(1, 0)
    ))
    expect(transport.requests[0][0:2] == (
        "session", (UDS_SESSION_DEFAULT,)
    ), "slot-specific artifact mismatch aborts the programming session")
    transport.respond(positive(
        UDS_SERVICE_DIAGNOSTIC_SESSION_CONTROL,
        bytes([UDS_SESSION_DEFAULT]),
    ))
    expect(controller.status == "FAULT" and
           events[-1]["event_code"] == "TARGET_MISMATCH",
           "wrong-slot artifact fails before any download data is sent")


def test_unsigned_artifact_is_rejected_locally():
    artifact = bytearray(make_artifact())
    artifact[64:128] = bytes(64)
    transport = TransportFixture()
    controller, _renders, events = make_controller(
        transport, bytes(artifact)
    )
    expect(not controller.start("unsigned.img"),
           "empty signature is rejected")
    expect(not transport.requests and controller.status == "FAULT",
           "invalid artifact never reaches CAN")
    expect(events[-1]["event_code"] == "INVALID_ARTIFACT",
           "local validation failure is observable")


def main():
    test_complete_update()
    test_target_mismatch_fails_before_download()
    test_unsigned_artifact_is_rejected_locally()
    print("PASS: sequential fail-closed GUI firmware update workflow")


if __name__ == "__main__":
    main()
