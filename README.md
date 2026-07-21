# STM32H7 CAN Control, RTC, Alarm, and Diagnostics

This project is an embedded control and observability system for the
**NUCLEO-H7A3ZI-Q**. An STM32H7A3 firmware application communicates with a
Python/PySide6 desktop GUI over Classic CAN, drives configurable cyclic CAN
traffic, controls board LEDs, accesses a PCA2131 real-time clock over I2C, and
reports RTC, alarm, CAN, and system health.

The current implementation emphasizes bounded memory, non-blocking state
machines, explicit wire formats, congestion-aware CAN delivery, fault recovery,
and debugger-friendly diagnostics.

## Main Features

- Two independently configured periodic CAN transmit slots
- Standard 11-bit and extended 29-bit slot identifiers
- Configurable cycle time and continuously wrapping 32-bit counters
- Remote control of two board LEDs
- PCA2131 date/time read and verified write operations
- PCA2131 alarm configuration with selective comparison fields
- Alarm status polling, one-shot event reporting, and flag clearing
- Software CAN TX queue with priority, coalescing, and bounded eviction
- RX validation with per-reason rejection counters and event logging
- FDCAN error-passive and bus-off observation
- Multi-phase bus-off recovery with exponential retry backoff
- Recovery verification using an actual TX-complete interrupt
- Periodic runtime diagnostic snapshots with sticky issue flags
- Health-gated independent watchdog with main-loop, CAN, and RTC check-ins
- Boot-time reset-cause capture including independent watchdog resets
- CRC-protected watchdog failure evidence retained across system resets
- 64-entry binary RAM event log with sequence numbers and CRC-16
- PySide6 GUI using `python-can` and PEAK PCAN

## Hardware and Configuration

| Item | Current configuration |
|---|---|
| Board | NUCLEO-H7A3ZI-Q |
| MCU | STM32H7A3ZI-Q, Cortex-M7 |
| System clock | 64 MHz from HSI through PLL1 |
| CAN peripheral | FDCAN1 in Classic CAN normal mode |
| CAN nominal bitrate | 500 kbit/s |
| CAN pins | PD0 RX, PD1 TX |
| FDCAN RX FIFO0 | 3 elements, 8 bytes each |
| FDCAN TX FIFO | 3 elements, FIFO operation |
| Software TX queue | 16 frames |
| RTC | PCA2131 at 7-bit I2C address `0x53` |
| I2C peripheral | I2C1, PB8 SCL and PB9 SDA |

Automatic CAN retransmission is enabled. The FDCAN1 interrupt must remain
enabled and must call `HAL_FDCAN_IRQHandler()` for error and TX-complete
callbacks to work.

## System Architecture

```text
+--------------------------- Desktop PC ----------------------------+
| PySide6 GUI -> python-can -> PEAK PCAN driver and USB adapter      |
+-------------------------------+-----------------------------------+
                                | Classic CAN, 500 kbit/s
                                |
+-------------------------------v-----------------------------------+
| STM32H7A3 firmware                                                 |
|                                                                    |
| can_app          RX validation, command dispatch, slots, LEDs      |
|   |                                                                |
|   +-> rtc_app    RTC and alarm policy/state machines               |
|   |     +-> pca2131       PCA2131 register-level I2C driver        |
|   |                                                                |
|   +-> can_protocol        Stable byte layouts and codecs           |
|   +-> can_transport       TX queue, freshness, priority            |
|   +-> can_recovery        bus-off restart and verification         |
|   +-> app_diagnostics     periodic RX/TX health snapshot           |
|   +-> app_log             binary event ring buffer                 |
|                                                                    |
| STM32 HAL -> FDCAN/I2C/GPIO registers -> transceiver/RTC/LEDs      |
+--------------------------------------------------------------------+
```

## Repository Structure

```text
Core/Startup/               Cortex-M7 reset handler and vector table
Core/Src/main.c             Hardware startup and application entry
Core/Inc|Src/can_app.*      CAN application and RX validation
Core/Inc|Src/can_protocol.* CAN identifiers and payload codecs
Core/Inc|Src/can_transport.* Congestion-aware transmit layer
Core/Inc|Src/can_recovery.* FDCAN error callbacks and recovery
Core/Inc|Src/pca2131.*      PCA2131 register driver
Core/Inc|Src/rtc_app.*      RTC and alarm state machines
Core/Inc|Src/app_log.*      Binary RAM event log
Core/Inc|Src/app_diagnostics.* Aggregated runtime diagnostics
Core/Inc|Src/app_watchdog.* Health-gated IWDG policy and timing diagnostics
Core/Inc|Src/app_watchdog_evidence.* Retained watchdog reset evidence
Core/Inc|Src/app_reset_reason.* Boot-time RCC reset-cause snapshot
Core/Src/stm32h7xx_it.c     Interrupt handlers
Core/Src/stm32h7xx_hal_msp.c Peripheral clocks, GPIO, and NVIC setup
python/can_gui.py           Desktop control and monitoring GUI
can_gui.ioc                 STM32CubeMX configuration
```

## Startup and Runtime

After the startup assembly initializes `.data`, clears `.bss`, and calls the C
runtime, `main()` performs:

1. MPU configuration
2. HAL and SysTick initialization
3. RCC reset-cause and retained watchdog-evidence capture
4. 64 MHz clock-tree configuration
5. GPIO, FDCAN1, I2C1, and IWDG1 hardware initialization
6. RAM event-log initialization and `SYSTEM_BOOT` record creation
7. CAN application, transport, filter, notification, and diagnostics setup
8. Non-blocking PCA2131 initialization scheduling
9. Initial system-status publication
10. Watchdog health-policy binding to the CubeMX-owned IWDG1 handle
11. Board LED, push-button, and COM initialization
12. Entry into the cooperative superloop

The main loop repeatedly calls `CAN_App_Process()`:

```text
CAN_Handle_BusOff_Recovery()
CAN_Transport_Process()
CAN_Process_Rx_Command()
RTC_Process()
System_Status_Process()
CAN_Process_TxSlots()
CAN_Transport_Process()
App_Diagnostics_Process()
```

There is no RTOS. Services use `HAL_GetTick()` deadlines and return without
deliberate blocking. I2C HAL calls are synchronous with a 10 ms timeout, so they
remain the principal bounded blocking operations in the superloop.

The watchdog evaluates progress every 250 ms and refreshes IWDG1 only after the
main loop, CAN application, and RTC service have all checked in. Its nominal
timeout is 4 seconds; target timing characterization remains required before
production sign-off. CubeMX owns the `hiwdg1` handle and peripheral setup;
`app_watchdog` owns only the health gate and refresh policy. See
`docs/WATCHDOG_ROADMAP.md` for the tracked validation plan.

Before each refresh decision, the watchdog stores its latest health state in
backup SRAM. The record uses a CRC-32 and last-written commit marker. On boot it
is trusted only when RCC also identifies IWDG1 as the reset source, and it is
consumed once to prevent stale evidence from being reported after later resets.
Evidence distinguishes a rejected health gate, a refresh error, and an apparent
hard superloop stall. A valid record creates a
`WATCHDOG_RESET_EVIDENCE` fault event whose `data_0` is the evidence cause and
whose `data_1` is the missing-heartbeat mask.

Unsigned elapsed-time comparisons are used for periodic work. Absolute
deadlines use signed subtraction where required so comparisons remain valid
across the 32-bit HAL tick wraparound.

## CAN Wire Protocol

All application frames are **Classic CAN data frames with exactly 8 payload
bytes**. Multi-byte integers are little-endian.

### Identifiers

| Direction | Identifier | Type | Purpose |
|---|---:|---|---|
| GUI -> MCU | `0x1894AABB` | Extended | All commands |
| MCU -> GUI | `0x551` | Standard | RTC operation status |
| MCU -> GUI | `0x556` | Standard | RTC date/time and health |
| MCU -> GUI | `0x557` | Standard | Slot and LED state |
| MCU -> GUI | `0x558` | Standard | RTC alarm event |

FDCAN hardware uses an exact-mask extended filter for `0x1894AABB` and rejects
unmatched standard, extended, and remote frames. Software still validates ID,
frame type, Classic/FD format, DLC, command, reserved fields, and value ranges.

### Commands

| Code | Command |
|---:|---|
| `0x01` | Configure slot 1 |
| `0x02` | Configure slot 2 |
| `0x10` | Control LED |
| `0x11` | Start/stop slot 1 counter |
| `0x12` | Start/stop slot 2 counter |
| `0x20` | Set RTC time |
| `0x21` | Set RTC date and time |
| `0x22` | Configure RTC alarm comparisons |

### Slot Configuration: `0x01` / `0x02`

```text
Byte 0     command
Byte 1     bit 0 enable, bit 1 extended ID; other bits must be zero
Bytes 2-5 CAN ID, uint32 little-endian
Bytes 6-7 cycle time in milliseconds, uint16 little-endian
```

Cycle time must be nonzero. Standard IDs are limited to `0x7FF`; extended IDs
are limited to `0x1FFFFFFF`. A new configuration stops the existing counter and
requires a new start command.

### Slot Counter Start: `0x11` / `0x12`

```text
Byte 0     command
Byte 1     reserved, zero
Bytes 2-5 counter limit, uint32 little-endian
Bytes 6-7 reserved, zero
```

A zero limit stops the slot. Otherwise, the first frame is eligible immediately
and contains the counter in bytes 0-3. The counter runs `1..limit` and wraps
continuously. Bytes 4-7 are zero.

### LED Command: `0x10`

```text
[0x10, LED number (1..2), state (0..1), 0, 0, 0, 0, 0]
```

Logical LED 1 maps to the green board LED and logical LED 2 to the red LED.

### RTC Date/Time Command: `0x21`

```text
[0x21, hundredth, second, minute, hour, day, month|weekday<<5, year]
```

Year is an offset from 2000. Firmware validation includes ranges, month length,
weekday, and leap-year handling before the PCA2131 write begins.

### RTC Alarm Command: `0x22`

```text
Byte 0 command
Byte 1 enable mask
Byte 2 second  (0..59 or zero when disabled)
Byte 3 minute  (0..59 or zero when disabled)
Byte 4 hour    (0..23 or zero when disabled)
Byte 5 day     (1..31 or zero when disabled)
Byte 6 weekday (0..6 or zero when disabled)
Byte 7 reserved, zero
```

Enable-mask bits are second `0x01`, minute `0x02`, hour `0x04`, day `0x08`, and
weekday `0x10`. Disabled fields are encoded into PCA2131 alarm registers with
the alarm-disable bit set. The driver converts the API's 24-hour value to the
RTC's active 12-hour or 24-hour register format.

### RTC Status: `0x551`

```text
Byte 0     semantic status code
Byte 1     HAL status
Bytes 2-5 HAL I2C error mask, little-endian
Bytes 6-7 zero
```

Success statuses include initialization `0xA1`, verified date/time write
`0xA2`, reconnection `0xA3`, and verified alarm write `0xA4`. Alarm failures
cover not-ready, invalid configuration, register write/readback failure,
verification mismatch, and alarm-flag clear failure (`0xEC..0xF1`).

### RTC Time: `0x556`

```text
Bytes 0-3 hour, minute, second, hundredth
Bytes 4-6 day, month, year offset
Byte 7 bits 0-2 weekday
       bit 5 calendar valid
       bit 6 RTC/I2C ready
       bit 7 oscillator-stop flag
```

### System Status: `0x557`

Byte 0 packs slot 1 running, slot 2 running, LED 1 on, and LED 2 on into bits
0-3. Bytes 1-4 repeat the individual values for easy inspection.

### Alarm Event: `0x558`

```text
Byte 0 event code 0x01 (triggered)
Byte 1 bit 0 AF, bit 1 AIE, bit 2 configuration valid
Bytes 2-4 event hour, minute, second
Bytes 5-7 event day, month, year offset
```

The firmware polls the PCA2131 alarm flag every 100 ms when the RTC is ready
and at least one alarm comparison is enabled. It sends one high-priority event
for an active flag, then clears the flag. A failed transmission or flag-clear
operation is counted in alarm diagnostics.

## CAN Application and RX Diagnostics

`can_app` owns two `CAN_TxSlot_t` instances and the command dispatcher. Each
slot groups configuration, lifecycle flags, ID type, CAN ID, period, counter,
and last-transmit tick.

Received frames are classified into exactly one rejection category:

- Wrong identifier or ID type
- Remote frame or CAN FD format
- Wrong DLC
- Unknown command
- Invalid command payload
- HAL receive failure

`CAN_App_RxStats_t` stores accepted and rejected counts plus the last reason and
command. Every actual rejection creates one binary `CAN_RX_REJECTED` log entry
in main-loop context; cumulative counters are not repeatedly logged.

## CAN Transport

The transport first uses the three-element FDCAN hardware TX FIFO. If hardware
cannot accept a frame, a fixed 16-entry software queue applies three policies:

- `SendClassic`: ordinary nonreplaceable FIFO traffic
- `SendClassicLatest`: periodic state replaces an equivalent queued frame
- `SendClassicHighPriority`: event traffic is inserted before periodic traffic

High-priority frames preserve FIFO order among themselves. When the software
queue is full, a high-priority frame may evict one replaceable periodic frame;
it never overwrites another nonreplaceable event.

Transport statistics track direct sends, queued sends, coalescing, priority
insertion, periodic eviction, overflows, HAL errors, occupancy, high-water mark,
and pending duration. A continuously nonempty queue lasting at least one second
creates a queue-stuck event.

## FDCAN Error Recovery

Interrupt callbacks remain short:

- Error-passive callback captures FDCAN `PSR` and `ECR` snapshots.
- Bus-off callback captures the registers and increments a monotonic event
  counter.
- TX-complete callback counts real completed transmissions.

The main loop logs transitions and performs recovery in resumable phases:

```text
STOP FDCAN -> START FDCAN -> re-enable notifications -> wait for TX complete
```

Successful earlier phases are not repeated when a later phase fails. Attempts
begin with a 200 ms delay and back off exponentially to a maximum of 5000 ms
when a new bus-off invalidates pending verification. Recovery is considered
successful only after an actual TX-complete interrupt occurs without a newer
bus-off event. A successful verification resets backoff to 200 ms.

## PCA2131 and RTC State Machines

The driver uses synchronous `HAL_I2C_Mem_Read/Write()` operations and returns a
`PCA2131_OperationStatus_t` containing a domain-specific result, HAL status,
HAL error mask, and optional recovery outcome.

Date/time writes follow a controlled sequence:

```text
read Control_1 -> set STOP -> reset prescaler -> write calendar -> clear STOP
```

If an intermediate operation fails, the driver attempts to release STOP and
reports the original and recovery failures separately.

The application layer uses non-blocking states and deadlines:

| Operation | Timing |
|---|---:|
| RTC initialization settle | 50 ms |
| Date/time readback verification | 20 ms |
| Alarm readback verification | 1 ms |
| Healthy RTC poll | 100 ms |
| Faulted RTC retry | 1000 ms |
| Alarm status poll | 100 ms |

Alarm configuration clears a previous alarm flag, writes five alarm registers,
waits for the verification deadline, reads the configuration back, and compares
every enabled state and value before reporting success.

## Runtime Diagnostics

`app_diagnostics` captures CAN RX and transport statistics every 100 ms. Its
snapshot includes uptime, update count, total rejected frames, raw RX/TX stats,
and sticky issue flags for:

- CAN RX HAL error
- CAN TX HAL error
- TX queue overflow
- TX queue stuck

Sticky flags preserve evidence after a transient fault has disappeared. The
module currently does not include RTC, alarm, or recovery statistics in the
combined snapshot; those modules expose separate getter APIs.

## Binary Event Log

`app_log` implements a 64-entry RAM ring buffer. Each record is compile-time
checked to be exactly 32 bytes:

```text
magic, sequence, uptime_ms, optional RTC epoch,
event code, source, severity, data_0, data_1,
CRC-16, commit marker
```

CRC-16 uses initial value `0xFFFF` and polynomial `0x1021`. The commit marker is
`0xA55A`. Interrupts are briefly masked with PRIMASK while shared ring state is
updated. Once full, new records overwrite the oldest and increment an overwrite
counter.

The current backend is RAM and is cleared at every boot. The 32-byte format,
magic, CRC, and commit marker prepare for a future Flash implementation but do
not currently provide persistence.

Currently generated log events include system boot, retained watchdog reset
evidence, rejected CAN frames, error-passive, bus-off, recovery success, and
distinct recovery failures. Event codes for RTC, alarm, queue, and timestamp
logging are defined but are not all wired to producers yet.

## Desktop GUI

The GUI in `python/can_gui.py` provides:

- PCAN channel and bitrate connection
- PCAN controller health monitoring
- Slot configuration and counter start
- LED control
- RTC date/time display and update
- RTC communication, OSF, and calendar-validity display
- Alarm comparison-field configuration and disable command
- Alarm write-status and event display
- System slot and LED status

The GUI defaults to `PCAN_USBBUS1` at `500000` bit/s. It accepts application
telemetry only from standard IDs `0x551`, `0x556`, `0x557`, and `0x558`.

## Build and Run

### Firmware

1. Open STM32CubeIDE.
2. Import this directory as an existing STM32CubeIDE project.
3. Review `can_gui.ioc`, especially FDCAN bitrate, filters, pins, I2C timing,
   and FDCAN1 NVIC configuration.
4. Build the `Debug` configuration.
5. Flash the NUCLEO-H7A3ZI-Q through ST-LINK.
6. Connect a CAN transceiver and correctly terminated 500 kbit/s bus.

For a command-line release build, install GNU Arm Embedded Toolchain and GNU
Make, then run:

```powershell
make CONFIG=release --jobs=2
```

The ELF, Intel HEX, raw binary, map, and disassembly outputs are written under
`build/release/`. Use `CONFIG=debug` for an unoptimized build with `DEBUG`
defined.

### GUI

```powershell
python -m pip install PySide6 python-can
python .\python\can_gui.py
```

A PEAK PCAN adapter and its platform driver are required for the configured
`interface="pcan"` backend.

### Watchdog CAN Stress

For target watchdog timing tests, close the GUI and run the controlled PCAN
command generator. It sends valid alternating LED commands and drains STM32
responses so sustained receive processing can be measured without manual input.

```powershell
.\python\.venv\Scripts\python.exe .\python\watchdog_can_stress.py `
  --duration 120 --period-ms 1
```

The defaults are `PCAN_USBBUS1`, `500000` bit/s, and LED 1. The final summary
reports achieved transmit/receive rates and missed host scheduling periods.

## Continuous Integration and Delivery

GitHub Actions runs independent CI jobs on pushes and pull requests:

- host-side watchdog policy tests
- a release firmware build using GNU Arm Embedded Toolchain
- Python syntax validation and all GUI regression scripts

Firmware artifacts are retained with each CI run. Pushing a tag whose name
starts with `v` runs the same verification, builds the firmware, packages the
Python application, generates SHA-256 checksums, and publishes a GitHub
release. GitHub Actions dependencies are monitored monthly by Dependabot.

## Engineering Constraints and Next Steps

- C and Python duplicate protocol constants. Generate both from one schema.
- Add host unit tests for codecs, calendar/alarm validation, queue policy,
  ring-buffer wrap, CRC vectors, and tick wraparound.
- Add hardware-in-loop tests for I2C NACK, alarm mismatch, missing CAN ACK,
  error-passive, bus-off, repeated recovery failure, and TX verification.
- Complete the timing and target fault-injection work in
  `docs/WATCHDOG_ROADMAP.md`.
- Extend diagnostics with recovery, RTC, alarm, and log health.
- Connect the remaining RTC/alarm/queue event codes to transition-based log
  producers.
- Add a versioned persistent Flash backend with page scanning, wear management,
  cache handling, and power-loss-safe commit rules.
- Add an explicit application-protocol version and compatibility handshake.

