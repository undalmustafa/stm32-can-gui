"""Generate sustained valid command traffic for target watchdog testing."""

import argparse
import secrets
import time

import can

from can_protocol_generated import (
    CMD_LED_CONTROL,
    CMD_SESSION_START,
    GUI_COMMAND_ID_EXT,
    GUI_COMMAND_ID_MASK_EXT,
    GUI_COMMAND_SESSION_MASK,
    GUI_COMMAND_SESSION_SHIFT,
    PROTOCOL_VERSION,
)


def command_id(sequence, session_tag):
    return (
        (GUI_COMMAND_ID_EXT & GUI_COMMAND_ID_MASK_EXT)
        | ((session_tag << GUI_COMMAND_SESSION_SHIFT)
           & GUI_COMMAND_SESSION_MASK)
        | (sequence & 0xFF)
    )


def parse_args():
    parser = argparse.ArgumentParser(
        description="Send valid LED commands at a fixed rate and drain replies."
    )
    parser.add_argument("--interface", default="pcan")
    parser.add_argument("--channel", default="PCAN_USBBUS1")
    parser.add_argument("--bitrate", type=int, default=500_000)
    parser.add_argument("--duration", type=float, default=120.0)
    parser.add_argument("--period-ms", type=float, default=1.0)
    parser.add_argument("--led", type=int, choices=(1, 2), default=1)
    return parser.parse_args()


def open_bus(args):
    options = {
        "interface": args.interface,
        "channel": args.channel,
        "bitrate": args.bitrate,
    }
    if args.interface == "pcan":
        options["auto_reset"] = True
    return can.Bus(**options)


def drain_receive_queue(bus):
    received = 0
    while bus.recv(timeout=0.0) is not None:
        received += 1
    return received


def run(args):
    if args.duration <= 0.0:
        raise ValueError("duration must be positive")
    if args.period_ms <= 0.0:
        raise ValueError("period-ms must be positive")

    period_s = args.period_ms / 1000.0
    sent = 0
    received = 0
    missed_periods = 0
    state = 0

    bus = open_bus(args)
    started = time.perf_counter()
    deadline = started + args.duration
    next_send = started
    try:
        session_nonce = secrets.randbits(32) or 1
        session_tag = session_nonce & 0xFF
        session_payload = [
            CMD_SESSION_START,
            session_nonce & 0xFF,
            (session_nonce >> 8) & 0xFF,
            (session_nonce >> 16) & 0xFF,
            (session_nonce >> 24) & 0xFF,
            PROTOCOL_VERSION,
            0,
            0,
        ]
        bus.send(can.Message(
            arbitration_id=command_id(0, session_tag),
            is_extended_id=True,
            is_fd=False,
            data=bytearray(session_payload),
        ))
        sequence = 1

        while time.perf_counter() < deadline:
            now = time.perf_counter()
            if now < next_send:
                time.sleep(next_send - now)
                now = time.perf_counter()

            if now - next_send >= period_s:
                missed = int((now - next_send) / period_s)
                missed_periods += missed
                next_send += missed * period_s

            state ^= 1
            message = can.Message(
                arbitration_id=command_id(sequence, session_tag),
                is_extended_id=True,
                is_fd=False,
                data=bytearray(
                    [CMD_LED_CONTROL, args.led, state, 0, 0, 0, 0, 0]
                ),
            )
            bus.send(message)
            sent += 1
            sequence = (sequence + 1) & 0xFF
            next_send += period_s
            received += drain_receive_queue(bus)
    finally:
        received += drain_receive_queue(bus)
        bus.shutdown()

    elapsed = time.perf_counter() - started
    print(f"Elapsed: {elapsed:.3f} s")
    print(f"Commands sent: {sent} ({sent / elapsed:.1f} frames/s)")
    print(f"Frames received: {received} ({received / elapsed:.1f} frames/s)")
    print(f"Host scheduling periods missed: {missed_periods}")


def main():
    args = parse_args()
    try:
        run(args)
    except KeyboardInterrupt:
        print("Stopped by user")
    except (can.CanError, OSError, ValueError) as error:
        raise SystemExit(f"Stress test failed: {error}") from error


if __name__ == "__main__":
    main()
