# STM32H7 CAN Control, RTC, Alarm, and Diagnostics

This project is an embedded control and observability system for the
**NUCLEO-H7A3ZI-Q**. An STM32H7A3 firmware application communicates with a
Python/PySide6 desktop GUI over Classic CAN, drives configurable cyclic CAN
traffic, controls board LEDs, generates a configurable PWM output, accesses a
PCA2131 real-time clock over I2C, and reports RTC, alarm, CAN, and system
health.

The protocol layer is generated from a single YAML schema so firmware and GUI
always share identical constants, identifiers, and status definitions.

The current implementation emphasizes bounded memory, non-blocking state
machines, explicit wire formats, congestion-aware CAN delivery, fault recovery,
and debugger-friendly diagnostics.

## Main Features

- Two independently configured periodic CAN transmit slots
- Standard 11-bit and extended 29-bit slot identifiers
- Configurable cycle time and continuously wrapping 32-bit counters
- Remote control of two board LEDs
- Configurable PWM output with runtime frequency and duty-cycle adjustment
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
- CAN-based STM32 event log synchronization with CRC verification
- Single-source YAML protocol definition with C and Python code generation
- PySide6 GUI using `python-can` and PEAK PCAN
- CSV event logging with formula-injection protection
- Unity-based host C test suite and Python GUI regression tests
- GitHub Actions CI/CD with automated releases

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
| PWM timer | TIM2 Channel 1, 1 MHz counter clock |
| PWM default | 10 kHz, 90% duty cycle |
| Watchdog | IWDG1, LSI/32 prescaler, ~1000 ms timeout |

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
|   +-> pwm_control         TIM2 frequency and duty-cycle driver     |
|                                                                    |
| watchdog             low-level IWDG1 init, refresh, reset detect   |
| app_watchdog         health-gated refresh policy and evidence      |
| app_reset_reason     boot-time RCC reset-cause capture             |
|                                                                    |
| STM32 HAL -> FDCAN/I2C/TIM/GPIO -> transceiver/RTC/PWM/LEDs       |
+--------------------------------------------------------------------+

+--------------------------- Protocol ------------------------------+
| protocol/can_protocol.yaml   single-source schema                  |
|   +-> generate_c.py          -> Core/Inc/can_protocol_generated.h  |
|   +-> generate_python.py     -> python/can_protocol_generated.py   |
+--------------------------------------------------------------------+
```

## Repository Structure

```text
protocol/
  can_protocol.yaml         Single-source protocol schema
  generate_c.py             C header code generator
  generate_python.py        Python module code generator

Core/Startup/               Cortex-M7 reset handler and vector table
Core/Src/main.c             Hardware startup and application entry
Core/Inc|Src/can_app.*      CAN application and RX validation
Core/Inc|Src/can_protocol.* CAN identifiers and payload codecs
Core/Inc/can_protocol_generated.h  Auto-generated protocol constants
Core/Inc|Src/can_transport.* Congestion-aware transmit layer
Core/Inc|Src/can_recovery.* FDCAN error callbacks and recovery
Core/Inc|Src/pca2131.*      PCA2131 register driver
Core/Inc|Src/rtc_app.*      RTC and alarm state machines
Core/Inc|Src/app_log.*      Binary RAM event log
Core/Inc|Src/app_diagnostics.* Aggregated runtime diagnostics
Core/Inc|Src/app_watchdog.* Health-gated IWDG policy and timing diagnostics
Core/Inc|Src/app_watchdog_evidence.* Retained watchdog reset evidence
Core/Inc|Src/app_reset_reason.* Boot-time RCC reset-cause snapshot
Core/Inc|Src/pwm_control.*  Configurable TIM2 PWM output driver
Core/Inc|Src/watchdog.*     Low-level IWDG1 hardware driver
Core/Src/stm32h7xx_it.c     Interrupt handlers
Core/Src/stm32h7xx_hal_msp.c Peripheral clocks, GPIO, and NVIC setup

python/
  can_gui.py                Desktop application entry point
  can_protocol_generated.py Auto-generated protocol constants
  requirements.txt          PySide6 and python-can
  run_gui.bat               Windows launch script
  can_gui_app/
    __init__.py             Package init
    main_window_view.py     Main window layout and navigation
    can_session.py          CAN bus session lifecycle
    can_connection_panel.py Connection and channel selection
    can_app_controller.py   CAN command dispatch and telemetry
    can_app_panel.py        Slot/LED/system control panel
    can_health.py           CAN health monitoring and indicators
    rtc_controller.py       RTC and alarm state management
    rtc_panel.py            RTC date/time and alarm UI
    event_log_panel.py      GUI event log and STM32 log display
    csv_event_logger.py     Daily CSV event logging
    stm32_log_sync.py       CAN-based MCU event log synchronization
    slot_widget.py          Reusable slot configuration widget
    application_timers.py   Periodic timer management
    protocol.py             Protocol helpers and definitions
    theme.py                UI theme and styling constants
  tests/
    test_gui_*.py           12 automated GUI regression tests

tests/
  CMakeLists.txt            CMake build for Unity C tests
  Makefile                  Make build for Unity C tests
  test_can_protocol.c       Protocol encoding/decoding tests
  test_can_transport.c      TX queue policy tests
  test_pca2131_validation.c Calendar and alarm validation tests
  stubs/                    HAL stubs for host compilation
  unity/                    Unity test framework

docs/
  WATCHDOG_ROADMAP.md       Watchdog validation plan and results

.github/
  workflows/ci.yml          CI: watchdog tests, firmware build, GUI tests
  workflows/release.yml     CD: tagged release with artifacts
  dependabot.yml            Monthly dependency monitoring

can_gui.ioc                 STM32CubeMX configuration
Makefile                    GNU Make firmware build
```

## Protocol Code Generator

The `protocol/can_protocol.yaml` schema is the single source of truth for all
CAN identifiers, command codes, status codes, bit flags, and field definitions.
Two generators consume this schema:

- `generate_c.py` produces `Core/Inc/can_protocol_generated.h` with C macros
  and enums (`CAN_Protocol_Command_t`, `CAN_Protocol_RtcStatusCode_t`,
  `CAN_Protocol_RtcAlarmEventCode_t`)
- `generate_python.py` produces `python/can_protocol_generated.py` with Python
  constants, dictionaries (`RTC_STATUS_DEFINITIONS`), and sets
  (`RTC_COMMUNICATION_FAULT_CODES`)

To regenerate after editing the schema:

```bash
python protocol/generate_c.py
python protocol/generate_python.py
```

Both generated files are committed to the repository so the project builds
without running the generators.

## Startup and Runtime

After the startup assembly initializes `.data`, clears `.bss`, and calls the C
runtime, `main()` performs:

1. MPU configuration
2. HAL and SysTick initialization
3. RCC reset-cause and retained watchdog-evidence capture
4. 64 MHz clock-tree configuration
5. GPIO, FDCAN1, I2C1, TIM2, and IWDG1 hardware initialization
6. PWM output initialization (TIM2 Channel 1 at 1 MHz, default 10 kHz/90%)
7. RAM event-log initialization and `SYSTEM_BOOT` record creation
8. CAN application, transport, filter, notification, and diagnostics setup
9. Non-blocking PCA2131 initialization scheduling
10. Initial system-status publication
11. Watchdog health-policy binding to the CubeMX-owned IWDG1 handle
12. Board LED, push-button, and COM initialization
13. Entry into the cooperative superloop

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

After each application cycle, the main loop registers a heartbeat check-in and
the watchdog evaluates the health-gate policy.

There is no RTOS. Services use `HAL_GetTick()` deadlines and return without
deliberate blocking. I2C HAL calls are synchronous with a 10 ms timeout, so they
remain the principal bounded blocking operations in the superloop.

## PWM Control

The PWM module drives TIM2 Channel 1 with a configurable counter clock
(typically 1 MHz). It supports:

- Dynamic frequency adjustment from 1 Hz to 1 MHz
- Duty cycle from 0% to 100%
- Non-blocking register updates via ARR and CCR1 (take effect at the next
  timer update event without stopping the output)
- Frequency rounding to minimize error against the counter clock

At the 1 MHz endpoint the 1 MHz timer clock provides only one count per
period, so the hardware can represent only 0% or 100% duty. Lower frequencies
provide progressively finer duty-cycle resolution; telemetry reports the
quantized duty actually produced.

A global `volatile PWM_Control_State_t g_pwmControlState` exposes operational
state, requested versus actual frequency, tick counts, and error codes for
real-time inspection in STM32CubeIDE Live Expressions.

The GUI's **PWM & Capture** page sends PWM commands on `0x1894AABB` (command
`0x40`) and displays the `0x055C` status readback. Frequency uses a logarithmic
1 Hz–1 MHz control, with duty-cycle control, presets, and explicit enable/stop
actions.

## PWM Input Capture and Loopback

TIM3 measures an external PWM signal on PA6 with a 1 MHz counter. Channel 1
captures the rising-edge period and Channel 2 captures high-pulse width; the
firmware publishes signal state, frequency, duty cycle, and the low 16 bits of
the edge counter on CAN ID `0x055D`. With a 16-bit timer, the practical
measurement range is approximately 15 Hz–500 kHz.

For a closed-loop check, connect the pins with one jumper:

```text
PA0 (TIM2_CH1 PWM output) ───── PA6 (TIM3_CH1 capture input)
```

Open **PWM & Capture** in the GUI. Live measurements are compared with the
reported output using a 2% frequency and 2 percentage-point duty tolerance.
**Run Frequency Sweep** checks 1 kHz, 10 kHz, 100 kHz, and 500 kHz and reports
the number of passing points. Do not drive PA6 from an external source while
the PA0–PA6 jumper is installed.

## Watchdog Architecture

The watchdog system has two layers:

**Low-level driver** (`watchdog.h/c`): Direct IWDG1 hardware control using
LSI/32 prescaler with a reload of 999 (~1000 ms timeout). Provides `Init`,
`Refresh`, and `Was_Reset_By_Watchdog` operations.

**Health-gated policy** (`app_watchdog.h/c`): Evaluates progress every 250 ms
and refreshes IWDG1 only after the main loop, CAN application, and RTC service
have all checked in. Its nominal timeout is 4 seconds; target timing
characterization is documented in `docs/WATCHDOG_ROADMAP.md`.

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
| MCU -> GUI | `0x55A` | Standard | Log info response |
| MCU -> GUI | `0x55B` | Standard | Cyclic heartbeat |

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
| `0x30` | Get event log info |
| `0x31` | Read event log by sequence number |

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

### Event Log Commands: `0x30` / `0x31`

The GUI can read the MCU's 64-entry RAM event log over CAN:

- `0x30` requests log metadata (entry count, sequence range, overwrite count)
- `0x31` requests a specific log entry by sequence number

Responses are sent as multi-frame fragments on IDs `0x70`–`0x84`. Error
responses use `0xF0`. Each record is reassembled and verified against CRC-16
(`0x1021` polynomial), magic marker (`0x4C4F4731`), and commit marker
(`0xA55A`). The GUI implements an automatic retry state machine for reliable
synchronization.

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

### Heartbeat: `0x55B`

A cyclic heartbeat frame is sent periodically to indicate the MCU is alive and
responsive. The GUI uses this to track connection health and detect
communication loss.

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

## STM32 Event Log Synchronization

The GUI can download and display the MCU's full event log over CAN using
`stm32_log_sync.py`. The protocol uses two commands:

- `CMD_LOG_GET_INFO` (`0x30`): requests log metadata
- `CMD_LOG_READ_SEQUENCE` (`0x31`): requests a record by sequence number

Telemetry responses arrive as multi-frame fragments on IDs `0x70`–`0x72` (log
info) and `0x80`–`0x84` (log records). Error responses use `0xF0`.

Each downloaded record is verified against CRC-16 (`0x1021`), magic marker
(`0x4C4F4731`), and commit marker (`0xA55A`). The synchronization engine
implements an automatic state machine with retry handling and writes results to
timestamped CSV files (`stm32_events_%Y%m%d_%H%M%S_%f.csv`).

## Desktop GUI

The GUI is organized as a `can_gui_app` Python package with modular
panels and controllers:

| Module | Purpose |
|---|---|
| `main_window_view.py` | Main window layout, navigation, and page switching |
| `can_session.py` | CAN bus session lifecycle management |
| `can_connection_panel.py` | PCAN channel and bitrate connection UI |
| `can_app_controller.py` | Command dispatch and telemetry decoding |
| `can_app_panel.py` | Slot configuration, counter, LED, and system control |
| `can_health.py` | CAN health monitoring, frame rates, and error indicators |
| `rtc_controller.py` | RTC and alarm state management |
| `rtc_panel.py` | RTC date/time display, update, and alarm configuration |
| `event_log_panel.py` | GUI event history and STM32 log display with filtering |
| `csv_event_logger.py` | Daily CSV event logging with injection protection |
| `stm32_log_sync.py` | CAN-based MCU event log synchronization |
| `slot_widget.py` | Reusable slot configuration widget |
| `application_timers.py` | Periodic timer management |
| `protocol.py` | Protocol helpers and constants |
| `theme.py` | UI theme and styling |

Key features:

- PCAN channel and bitrate connection
- PCAN controller health monitoring
- Slot configuration and counter start
- LED control
- RTC date/time display and update
- RTC communication, OSF, and calendar-validity display
- Alarm comparison-field configuration and disable command
- Alarm write-status and event display
- System slot and LED status
- STM32 event log download with CRC verification
- Task-oriented Control, Live Data, and Logs & Errors pages
- Filterable recent GUI event and error history with severity badges
- Persistent CAN connection and health status across every page
- Plain-language CAN, RTC, and log health summaries with technical tooltips
- Daily CSV event logging with formula-injection protection

The GUI defaults to `PCAN_USBBUS1` at `500000` bit/s. It accepts application
telemetry only from standard IDs `0x551`, `0x556`, `0x557`, `0x558`, `0x55A`,
and `0x55B`.

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

```bash
make CONFIG=release --jobs=2
```

The ELF, Intel HEX, raw binary, map, and disassembly outputs are written under
`build/release/`. Use `CONFIG=debug` for an unoptimized build with `DEBUG`
defined.

### GUI

```bash
python -m pip install PySide6 python-can
python python/can_gui.py
```

A PEAK PCAN adapter and its platform driver are required for the configured
`interface="pcan"` backend.

On Windows, a launch script is provided:

```powershell
.\python\run_gui.bat
```

### Running Tests

**C firmware tests** (requires a host C compiler and Unity):

```bash
cd tests
make
```

Or with CMake:

```bash
cd tests
cmake -B build && cmake --build build && ctest --test-dir build
```

**Python GUI tests**:

```bash
python -m pytest python/tests/
```

**Host watchdog policy tests**:

```bash
python tests/host/run_watchdog_tests.py
```

### Watchdog CAN Stress

For target watchdog timing tests, close the GUI and run the controlled PCAN
command generator. It sends valid alternating LED commands and drains STM32
responses so sustained receive processing can be measured without manual input.

```bash
python python/watchdog_can_stress.py --duration 120 --period-ms 1
```

The defaults are `PCAN_USBBUS1`, `500000` bit/s, and LED 1. The final summary
reports achieved transmit/receive rates and missed host scheduling periods.

## Continuous Integration and Delivery

GitHub Actions runs independent CI jobs on pushes and pull requests:

- **`watchdog-tests`**: executes host watchdog policy tests on Python 3.13 /
  Ubuntu 24.04
- **`firmware`**: builds Debug and Release firmware using `gcc-arm-none-eabi`;
  verifies Debug includes fault-injection hooks while Release excludes them;
  retains build artifacts (`.elf`, `.hex`, `.bin`, `.map`) for 14 days
- **`gui`**: validates Python syntax via `compileall` and runs all GUI
  regression test scripts

Pushing a tag whose name starts with `v` triggers the release workflow, which
runs the same verification, builds Release firmware, packages the Python GUI
as `can_gui-python.zip`, generates SHA-256 checksums, and publishes a GitHub
release using `gh release create`.

GitHub Actions dependencies are monitored monthly by Dependabot.

## Engineering Constraints and Next Steps

- ~~C and Python duplicate protocol constants.~~ ✅ Generated from
  `protocol/can_protocol.yaml`.
- ~~Add host unit tests for codecs, calendar/alarm validation, queue policy.~~
  ✅ Unity C test suite and Python GUI regression tests added.
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
