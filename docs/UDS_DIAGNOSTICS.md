# UDS Diagnostic Contract

Physical UDS addressing runs over ISO-TP on Classic CAN:

- Tester to MCU request ID: standard `0x7E0`
- MCU to tester response ID: standard `0x7E8`
- ISO-TP transport capacity: 512 bytes

## Supported services

| SID | Service | Support |
|---:|---|---|
| `0x10` | Diagnostic Session Control | Default `0x01`, programming `0x02`, extended `0x03` |
| `0x22` | Read Data by Identifier | One or multiple DIDs |
| `0x31` | Routine Control | Inactive-slot erase routine `0xFF00` |
| `0x34` | Request Download | 32-bit address and 32-bit artifact size |
| `0x36` | Transfer Data | Block counter plus up to 256 data bytes |
| `0x37` | Request Transfer Exit | Signature/validation and pending-slot commit gate |
| `0x3E` | Tester Present | Subfunction `0x00` |

The suppress-positive-response bit is supported for `0x10` and `0x3E`.
Programming and extended sessions return to the default session after a
five-second S3 timeout. The server reports P2Server as 50 ms and P2*Server as
5,000 ms.

## Firmware download contract

Programming services operate only in programming session `0x02`. The sequence
is `31 01 FF00` to start erase, `31 03 FF00` to poll results, `0x34`, ordered
`0x36` blocks, and `0x37` to exit transfer. The erase response returns status
(`0=ready`, `1=in progress`, `2=failed`), target slot, slot base address, and
capacity. The device always selects the slot opposite the running image; the
tester cannot choose the active slot.

The artifact size must be aligned to the 16-byte flash program unit, include
at least the 1 KiB image header, and fit the inactive slot. The maximum block
length reported by `0x34` is 258 bytes: SID, block sequence counter, and up to
256 bytes of data. Repeating the most recently accepted counter receives an
idempotent acknowledgement. Missing or out-of-order counters are rejected
with NRC `0x73`.

The first 1 KiB header remains in RAM during transfer. The payload is written
to the inactive slot first. Transfer succeeds only when the `0x37` callback
validates signature and digest, writes the header last, and persists the
redundant boot-control pending record. Leaving programming session or hitting
S3 timeout aborts an open download. The STM32 flash and embedded public-key
implementation is still pending; without a finalize callback, the service
fails closed with conditions-not-correct (`0x22`).

## Data Identifiers

All multi-byte fields use UDS big-endian order.

| DID | Name | Layout |
|---:|---|---|
| `0xF100` | Protocol info | UDS version (u8), CAN protocol version (u8), log version (u8), ISO-TP capacity (u16), request ID (u16), response ID (u16) |
| `0xF101` | Startup health | expected/ready/failed masks (3 x u32), first failed resource (u32), first failure result (u32), degraded (u8) |
| `0xF102` | Runtime health | uptime, latched issues, rejected frames, RX lost, TX overflow, ISO-TP protocol errors, ISO-TP transport failures (7 x u32) |
| `0xF103` | Reset reason | decoded flags (u32), raw RCC RSR (u32), capture count (u32) |

## Negative responses

The server returns `0x11` for unsupported service, `0x12` for unsupported
subfunction, `0x13` for invalid length, `0x14` for an oversized response,
`0x22` for unmet conditions, and `0x31` for an unknown DID. Firmware download
also uses `0x24` for sequence error, `0x33` for denied authorization, `0x71`
for suspended transfer, `0x72` for programming failure, `0x73` for block
counter error, and `0x7F` for an invalid session. The wire format is
`7F <request SID> <NRC>`.

## Python client and GUI behavior

The desktop application sends diagnostic requests and flow-control frames on
standard ID `0x7E0`. It consumes `0x7E8` responses in the existing 50 ms CAN
RX poll, without opening a second bus reader. ISO-TP response timeout is one
second. Invalid PCI, length, sequence, or transport state terminates the
current request fail-closed.

The Diagnostics page reads the four DIDs in two groups to stay within the
Classic CAN single-frame request limit: `F100+F101`, then `F102+F103`. One
group is requested each second. Long responses are reassembled from first and
consecutive frames, and the tester sends flow-control CTS (`BS=0`, `STmin=0`).
Repeated communication errors are logged only on state transitions; a valid
response emits a recovery event.
