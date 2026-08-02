# CAN Bootloader Architecture

This document freezes the flash and image contracts for signed A/B firmware
updates. The HAL-independent image validator, rollback policy, boot target,
Cortex-M7 handoff path, power-loss-safe flash persistence, UDS download state
machine, and GUI Flash workflow are implemented. The STM32 update finalizer,
embedded public-key verifier, and offline signing pipeline remain open.

The STM32H7A3ZITxQ provides 2 MiB of dual-bank user flash. The layout follows
the 8 KiB user-flash sector boundaries defined by RM0455:

| Region | Start | End, exclusive | Size | Bank |
|---|---:|---:|---:|---:|
| Bootloader | `0x08000000` | `0x08020000` | 128 KiB | 1 |
| Application A | `0x08020000` | `0x08100000` | 896 KiB | 1 |
| Application B | `0x08100000` | `0x081E0000` | 896 KiB | 2 |
| Boot control | `0x081E0000` | `0x08200000` | 128 KiB | 2 |

Source: [ST RM0455 — STM32H7A3/7B3 reference manual](https://www.st.com/resource/en/reference_manual/dm00463927-stm32h7a3-b3-and-stm32h7b0-value-line-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf).

The source of truth is `Core/Inc/boot_memory_map.h`. Compile-time assertions
reject gaps, overflow, and overlap between managed regions.

## Image layout

Each slot contains an independently linked application image:

```text
slot base + 0x000  128-byte signed manifest
slot base + 0x080  reserved header padding
slot base + 0x400  Cortex-M7 vector table and image payload
```

The 1 KiB header preserves the natural Cortex-M7 alignment of the 171-word
vector table. The linker rejects a vector table that exceeds this reservation.
Manifest fields are little-endian:

| Offset | Size | Field |
|---:|---:|---|
| `0x00` | 4 | Magic `IMG1` (`0x31474D49`) |
| `0x04` | 2 | Manifest format version |
| `0x06` | 2 | Header size, 1,024 in format version 1 |
| `0x08` | 4 | Image payload size |
| `0x0C` | 4 | Vector table address |
| `0x10` | 4 | Thumb reset-handler address |
| `0x14` | 4 | Monotonic security counter |
| `0x18` | 4 | Product build version |
| `0x1C` | 4 | Flags; must be zero in format version 1 |
| `0x20` | 32 | SHA-256 digest of the image payload |
| `0x40` | 64 | Signature over the first 64 manifest bytes |

The signature covers canonical bytes `0x00..0x3F`, including the digest. Image
size, placement, security counter, build version, and digest are therefore
authorized together. A missing signature callback is a hard validation
failure; unsigned images are never accepted as pending.

## Pre-boot validation

`Boot_Image_Validate()` enforces this order:

1. Slot, reader, digest, signature, and RAM-region dependencies are valid.
2. Magic, format, fixed header size, and reserved flags are valid.
3. The payload range fits the slot without integer overflow.
4. The vector table is exactly at `slot base + 1024`.
5. The security counter is not below the persistent minimum.
6. Initial MSP is eight-byte aligned and inside an allowed RAM region.
7. Vector reset handler equals the manifest entry, has the Thumb bit set, and
   points inside the payload.
8. Manifest signature verification succeeds.
9. Payload SHA-256 digest verification succeeds.

A failed image is never branched to. Each failure has a distinct result code
for the boot policy and recovery layer.

## Redundant boot control and rollback

The first two 8 KiB sectors in the boot-control region hold independent
records:

- Record 0: `0x081E0000`
- Record 1: `0x081E2000`

Each 64-byte record contains magic/format/size, wrap-safe generation,
confirmed and pending slots, attempts remaining, security counters, build
versions, CRC-32, and a commit marker. Every reserved field must be zero.

When persisting new state, the current record remains untouched. The other
sector is erased and verified by readback. Because the STM32H7A3 program unit
is 128 bits (16 bytes), the first three flashwords (`0..47`) are programmed
and verified first. The last flashword contains reserved data, CRC, and the
`0xC04E7A5A` commit marker. It is programmed last as the single acceptance
point, then the complete record is verified. A four-byte marker write is not
used because the HAL and hardware require a complete flashword.

Power loss before the final flashword leaves the new marker erased and the
older generation remains selected. Power loss during the final flashword
accepts the new record only when both CRC and marker are complete. Flash is
relocked on every error path. Corrupt, partial, or conflicting equal-generation
records are rejected fail-closed.

`Boot_Flash_PersistControl()` enforces erase, erase verification, three body
program operations, body verification, final commit programming, and final
verification. The STM32 adapter erases only sector-aligned managed addresses
outside the bootloader and rejects unaligned or out-of-range writes. Before
CPU readback it runs the hardware flash CRC engine, turning partially
programmed flashword ECC double errors into a fail-closed `CRCRDERR` result
instead of a HardFault caused by a direct CPU read. ST documents the direct
read behavior in its [STM32H7 flash ECC example](https://community.st.com/stm32-mcus-60/injecting-and-handling-ecc-errors-in-stm32h7-flash-memory-141263).

The update and rollback policy is:

1. Only an image in the slot opposite the confirmed slot and with a security
   counter above the persistent minimum may become pending.
2. A pending image receives three boot attempts.
3. Attempts are decremented and persisted before every application jump.
4. The application confirms its own slot/security/build identity; confirmation
   promotes pending to confirmed and advances the anti-rollback minimum.
5. Exhausted attempts or invalid pending validation clear pending and select
   the last confirmed image.
6. If the confirmed image is also invalid, no arbitrary unconfirmed image is
   started; the bootloader remains in recovery mode.

The core layer makes no HAL calls. STM32H7A3 erase/program/read operations are
isolated in the platform adapter.

## Linker profiles and application handoff

The common section layout remains in `STM32H7A3ZITXQ_FLASH.ld`; target
profiles define only flash boundaries:

- `linker/STM32H7A3ZITXQ_BOOT.ld`: `0x08000000..0x08020000`
- `linker/STM32H7A3ZITXQ_SLOT_A.ld`: payload
  `0x08020400..0x08100000`
- `linker/STM32H7A3ZITXQ_SLOT_B.ld`: payload
  `0x08100400..0x081E0000`

Application profiles deliberately exclude the 1 KiB manifest/header area.
`make slot-a` and `make slot-b` produce applications linked for different
addresses. Do not flash those raw artifacts directly; the signed image
packager is required.

The startup vector symbol is exported and `SystemInit()` sets VTOR from the
linker-selected `g_pfnVectors` address. Standalone, Slot A, and Slot B builds
therefore share the same startup code without fixed offset macros.

`Boot_Target_Run()` follows this fail-closed sequence:

1. Read both boot-control records and select the newest valid generation.
2. Accept only a slot identity/security counter/build version recorded as
   confirmed or pending.
3. Persist any state change made by rollback policy before attempting a jump.
4. Run a second handoff preflight for the selected image.
5. Pass the validated vector/MSP/entry plan to the platform jump callback.

The Cortex-M7 backend first calls the bootloader peripheral/DMA quiesce
callback and cancels handoff if it fails. It then masks global interrupts,
stops SysTick, clears every NVIC enable and pending bit, disables caches,
installs VTOR, and executes barriers. Finally it restores
CONTROL/PSP/BASEPRI/FAULTMASK reset state, loads application MSP, and branches
to the Thumb reset handler. If the branch returns, interrupts remain disabled
and the backend enters a safe halt.

## Firmware download path

The implemented desktop-to-core path is:

```text
Flash panel
  -> fail-closed artifact preflight
  -> UDS programming session
  -> inactive-slot erase routine
  -> RequestDownload
  -> ordered, retry-safe TransferData blocks
  -> RequestTransferExit
  -> finalizer callback
```

The first 1 KiB header is held back while the payload is written. The finalizer
must validate the embedded signature and payload digest, program the header
last, verify it, schedule the slot as pending, and persist boot control. Until
that callback is wired to the STM32 and a real public key, the product remains
fail-closed at transfer exit.

Hardware-free tests cover manifest validation, slot bounds, rollback, power
loss at every control-record program point, UDS ordering and duplicate blocks,
desktop timeout retry, wrong-slot rejection, and unsigned artifact rejection.
They do not replace target flash, reset, confirmation, or rollback tests.

## Remaining slices

1. Connect the STM32 slot writer and embedded signature verifier to the UDS
   finalizer, including pending-record persistence.
2. Add the release image packager, offline signing, embedded public key, and
   signed release artifacts to CI/release.
3. Validate erase/program/reset/confirmation/rollback on target hardware.

`STM32H7A3ZITXQ_FLASH.ld` remains the standalone development profile. Do not
use the managed A/B addresses for production flashing until the finalizer and
signed artifact chain are complete.
