# STM32H7 Automotive I/O and CAN Console

Firmware and a desktop GUI for demonstrating automotive-style local inputs, CAN
control, PWM generation, input capture, RTC functions, diagnostics, and
fail-safe behavior.

The target is an **STM32 NUCLEO-H7A3ZI-Q**. A **TIC12400-Q1** monitors 23
physical switches, while the GUI communicates with the board over Classic CAN.
Normal outputs are controlled remotely; selected physical inputs can override
them even if CAN is unavailable.

## What the system does

- Controls two CAN transmit slots and the Nucleo user LEDs.
- Generates PWM from `16 Hz` to `500 kHz` with configurable duty cycle.
- Measures an external PWM signal using timer input capture.
- Runs an automatic PWM-to-capture built-in test.
- Reads 23 switches through the TIC12400-Q1 over SPI.
- Applies local safety overrides for PWM, LEDs, and CAN transmit slots.
- Manages time and alarms with a PCA2131 RTC.
- Reports telemetry, faults, and retained event logs over CAN.
- Supports SocketCAN on Linux and PCAN-Basic on Windows.
- Recovers from CAN and TIC12400 communication faults without stopping local
  safety behavior.
- Continues in degraded mode when an optional I2C, SPI, timer, capture, or
  debug-COM initialization fails; the yellow LED and retained event log expose
  the failed startup resource.

## System overview

```text
Desktop GUI
    |
    | Classic CAN, 500 kbit/s
    v
STM32H7 application
    +-- FDCAN1 ---- CAN transceiver ---- CAN bus
    +-- SPI3 ------ TIC12400-Q1 -------- 23 switches
    +-- I2C1 ------ PCA2131 RTC
    +-- TIM2 ------ PWM output
    +-- TIM3 ------ input capture
    +-- GPIO ------ user LEDs and B1 service button
```

The GUI and CAN commands own normal operation. TIC12400 switch inputs form a
separate local policy layer and can override selected remote requests.

## Hardware

### Required equipment

- NUCLEO-H7A3ZI-Q
- External 3.3 V-compatible CAN transceiver
- Properly terminated CAN bus
- TIC12400-Q1 switch board
- PCA2131 RTC board
- A protected external supply for the TIC12400 `VS` rail
- Optional jumper for PWM loopback testing

The firmware uses the Nucleo ST-LINK 8 MHz MCO on `PH0/OSC_IN` as an HSE
bypass clock. This reference drives the PLL and the 500 kbit/s FDCAN timing.
Keep the on-board ST-LINK clock routing enabled; a board without that clock
will intentionally remain in the reset path instead of running CAN from HSI.

### Core connections

| Function | STM32 pin | Nucleo location | Configuration |
|---|---:|---:|---|
| FDCAN1 RX | PD0 | MCU header | Alternate function |
| FDCAN1 TX | PD1 | MCU header | Alternate function |
| PWM output | PA0 | D32 / CN10 pin 29 | TIM2 CH1 |
| Capture input | PA6 | D12 / CN7 pin 12 | TIM3 CH1 |
| RTC SCL | PB8 | MCU header | I2C1 SCL |
| RTC SDA | PB9 | MCU header | I2C1 SDA |
| Service authorization | PC13 | B1 button | Active high |

FDCAN pins are logic-level signals. Connect them to a CAN transceiver; do not
connect them directly to CANH and CANL.

### TIC12400-Q1 connections

| TIC12400 signal | STM32 pin | Nucleo location | Configuration |
|---|---:|---:|---|
| `SI / MOSI` | PC12 | CN8 pin 10 | SPI3 MOSI |
| `SCLK` | PC10 | CN8 pin 6 | SPI3 SCK |
| `CS` | PA4 | CN7 pin 17 | GPIO output |
| `SO / MISO` | PC11 | CN8 pin 8 | SPI3 MISO |
| `INT` | PG6 | CN10 pin 13 | Falling-edge EXTI, 3.3 V pull-up |
| `RESET` | PA2 | CN10 pin 11 | GPIO output; low in normal operation |
| `VDD` | — | Nucleo 3V3 | Digital supply |
| `DGND / AGND` | — | Nucleo GND | Common ground |
| `VS` | — | External supply | 4.5–35 V device range; 12 V recommended |

When using a bench supply, connect its positive terminal to `VS` and its
negative terminal to the same ground used by the TIC12400 board and Nucleo.
Set the current limit before enabling the supply. Do not power `VS` from a
GPIO pin.

The supplied switch board already contains the input networks. Operate its
physical switches; do not inject an external voltage into those inputs.

### PWM loopback

For capture testing, connect:

```text
D32 / PA0 / TIM2_CH1  ->  D12 / PA6 / TIM3_CH1
```

Do not connect another signal generator to PA6 at the same time. PA6 must
remain within the STM32 3.3 V input limits.

## Quick start

### 1. Build and flash the firmware

Open the project in STM32CubeIDE, build it, and flash the
NUCLEO-H7A3ZI-Q. The CubeMX configuration is stored in `can_gui.ioc`.

Command-line builds are also available:

```bash
make CONFIG=release --jobs=2
```

For a debugger-friendly build:

```bash
make CONFIG=debug --jobs=2
```

Build output is written under `build/`.

### 2. Prepare the CAN interface

The firmware uses **Classic CAN at 500 kbit/s**. The physical bus must have a
common ground and correct termination, normally 120 ohms at each end.

On Linux, configure SocketCAN:

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 restart-ms 100
sudo ip link set can0 up
ip -details -statistics link show can0
```

Replace `can0` if your adapter uses another interface name.

On Windows, install the PEAK driver and PCAN-Basic runtime for the PCAN
adapter.

### 3. Start the GUI

On Linux, create a virtual environment and install the dependencies:

```bash
python3 -m venv python/.venv
python/.venv/bin/python -m pip install -r python/requirements.txt
```

Run:

```bash
python/.venv/bin/python python/can_gui.py
```

On Windows, install the dependencies and use the launcher:

```powershell
py -m pip install -r python\requirements.txt
.\python\run_gui.bat
```

Select `SocketCAN (Linux)` or `PCAN (Windows)`, choose the interface, set
`500000 bit/s`, and press **Connect**.

## Using the application

The GUI is divided into five task-oriented pages:

| Page | Purpose |
|---|---|
| Control | LEDs, CAN transmit slots, RTC, and alarms |
| Live Data | Board telemetry and communication state |
| PWM / Capture | PWM generation, measurement, and built-in test |
| TIC12400 | Switch state and supported input-polarity configuration |
| Logs / Errors | Retained events, faults, and synchronization status |

### Remote control and physical overrides

GUI and CAN commands set the requested state. The firmware then applies the
TIC12400 policy and produces an effective state:

| Physical input | Closed-switch action |
|---|---|
| IN0 | Force LED1 on |
| IN1 | Inhibit PWM output |
| IN2 | Inhibit CAN transmit slot 1 |
| IN3 | Inhibit CAN transmit slot 2 |
| IN4–IN23 | Monitored and reported; reserved for future policies |

Opening an override switch returns that function to its last requested remote
state. On this switch board:

- **Left = closed**
- **Center or right = open**
- **IN12 is unavailable** because its input resistor is not fitted

If TIC12400 data becomes invalid, PWM and both configurable CAN transmit slots
are disabled. Essential CAN diagnostics remain active. Local monitoring and
policy execution continue even when the external CAN bus is unavailable.

### TIC12400 monitoring

The TIC12400-Q1 replaces many direct MCU inputs with a protected, diagnostic
24-channel switch monitor. This application uses:

- SPI register access
- Hardware comparator sampling
- Three-sample firmware qualification
- Both-edge switch-change interrupts
- Periodic status polling as an interrupt fallback
- Communication-failure detection and automatic reinitialization

Inputs IN0–IN9 can be configured as ground-connected (`−`) or
battery-connected (`+`) switches. IN10–IN23 are fixed as ground-connected,
and IN12 is unavailable. The selected polarity must match the physical input
circuit; the onboard carrier switches are ground-connected.

The GUI intentionally hides raw register snapshots. It presents only the
useful operator state and confirmed polarity. To change polarity:

1. Confirm that the external input is wired for the selected topology.
2. Press Nucleo **B1** to open the service-access window.
3. Select `− Ground` or `+ Battery` for IN0–IN9 and press **Apply switch
   polarity**.
4. Wait for **Polarity profile applied**. The GUI reports the setting returned
   by the MCU rather than assuming the command succeeded.

Polarity changes are runtime-only. An MCU reset restores every fitted input to
the ground-connected carrier default.

### PWM and input capture

TIM2 generates PWM using a 1 MHz timer timebase. TIM3 measures frequency and
duty cycle with the same nominal timebase.

| Item | Supported value |
|---|---|
| PWM frequency | 1 Hz–1 MHz |
| PWM duty | 0–100% |
| Practical capture range | Approximately 16 Hz–500 kHz |
| Startup state | PWM stopped |
| Default setting | 10 kHz, 90% |

Timer values are integers. Requested values that cannot be represented exactly
are quantized to the closest achievable period and pulse width. This is most
visible at high frequencies: at 500 kHz, one period is only two timer ticks;
at 1 MHz, it is one tick.

Duty values of 0% and 100% are handled as constant output levels rather than
normal edge-producing PWM. Input capture cannot calculate a frequency from a
constant level.

The built-in loopback test checks:

- 1 kHz, 10 kHz, 100 kHz, and 500 kHz at 50%
- 10 kHz at 10%, 25%, 50%, 75%, and 90%
- Signal-loss detection after PWM is stopped

The default tolerance is 2% for frequency and 2 percentage points for duty
cycle.

### Service authorization

State-changing commands are accepted during a four-minute service window.
Press the Nucleo blue **B1** button to open that window.

Safe commands remain available while locked, including disabling transmit
slots, turning LEDs off, stopping PWM, cancelling the built-in test, and
reading logs. B1 is a physical maintenance authorization mechanism, not
cryptographic authentication.

## CAN interface

Every application frame uses Classic CAN with exactly 8 data bytes.
Multi-byte values are little-endian.

### Telemetry

| CAN ID | Meaning |
|---:|---|
| `0x551` | RTC status |
| `0x552` | TIC12400 status |
| `0x553` | Reserved engineering ADC telemetry |
| `0x554` | TIC12400 switch bitmap |
| `0x555` | Applied TIC12400 polarity profile |
| `0x556` | RTC date and time |
| `0x557` | Requested, physical, and effective control state |
| `0x558` | RTC alarm event |
| `0x55A` | Event-log response |
| `0x55B` | Heartbeat and system state |
| `0x55C` | PWM state |
| `0x55D` | Input-capture measurement |
| `0x55E` | Built-in-test status |
| `0x55F` | Built-in-test point result |
| `0x560` | MCU FDCAN RX FIFO pressure and loss telemetry |

### Commands

| Command | Meaning |
|---:|---|
| `0x01`, `0x02` | Configure CAN transmit slots |
| `0x10` | Set LED state |
| `0x11`, `0x12` | Start slot countdowns |
| `0x20` | Set RTC time |
| `0x21` | Set RTC date and time |
| `0x22` | Configure RTC alarm |
| `0x30` | Read event-log information |
| `0x31` | Read an event by sequence number |
| `0x40` | Configure or stop PWM |
| `0x41` | Start or cancel the PWM built-in test |
| `0x50` | Configure TIC12400 IN0–IN9 switch polarity |

Every GUI command uses the fixed extended ID `0x1894AABB`; payload byte 0
selects the command. The MCU acknowledges command execution on standard ID
`0x550`, including a CRC-8 token calculated from the request payload. The GUI
sends one command at a time, queues later requests, and retries once if an
acknowledgement is not received.

This fixed-ID transport is protocol version 2. Update the GUI and reflash the
firmware together; protocol version 1 used dynamic command identifiers.

The complete byte-level definition is
[protocol/can_protocol.yaml](protocol/can_protocol.yaml). Treat that schema,
not this summary, as the protocol source of truth.

## Fault handling and diagnostics

- **CAN:** uses a 32-element blocking RX FIFO with a 24-element watermark,
  reports FIFO pressure/loss on `0x560`, detects error-passive and bus-off
  states, verifies completed transmissions, and performs rate-limited
  recovery. If FDCAN cannot start, local TIC12400, PWM, capture, RTC, and
  watchdog services continue.
- **TIC12400:** invalidates switch data on a failed service operation. After
  three consecutive failed batches it enters offline recovery, starting at
  500 ms and backing off to 8 s.
- **Watchdog:** IWDG uses a `/128` prescaler and reload value `999`, giving a
  nominal timeout of about 4 seconds. It is refreshed only when the main loop,
  CAN task, and RTC task remain healthy. Debug builds freeze it while halted.
- **Event log:** stores 64 CRC-protected records in 2 KiB of retained RAM and
  synchronizes them to the GUI for CSV export.

GUI traffic health is based on the age of the most recent valid MCU frame:

| Frame age | GUI state |
|---:|---|
| Less than 2 s | Healthy |
| 2–5 s | Stale warning |
| 5 s or more | Timeout |

Three consecutive valid frames are required to recover from a stale or timeout
state.

## Tests

Run the host-side C tests:

```bash
make -C tests test
```

Run the Python GUI regression scripts through the test Makefile:

```bash
make -C tests test-python
```

Hardware validation still requires the target board, CAN transceiver,
TIC12400 switch board, RTC board, and the PWM loopback connection.

## Repository layout

```text
Core/                 STM32 application and interrupt code
Drivers/              STM32 HAL and CMSIS
protocol/             CAN protocol schema and code generators
python/               Cross-platform desktop GUI and Python tests
tests/                Host-side firmware unit tests
docs/                 Detailed roadmaps and captured test evidence
can_gui.ioc           STM32CubeMX configuration
Makefile              Command-line firmware build
```

When changing the CAN schema, regenerate both protocol implementations:

```bash
python protocol/generate_c.py
python protocol/generate_python.py
```

Generated files are committed to the repository. CI checks that generated code
matches the schema.

## Detailed documentation

- [PWM and input-capture roadmap](PWM_ROADMAP.md)
- [TIC12400-Q1 roadmap](TIC12400_ROADMAP.md)
- [Watchdog roadmap](docs/WATCHDOG_ROADMAP.md)
- [TIC12400 characterization data](docs/tic12400_characterization_20260728.csv)

## Practical limitations

- IN12 is not populated on the current switch board.
- TIC12400 battery-switch polarity is available only on IN0–IN9 and requires
  a suitable battery-connected external input circuit. The onboard carrier
  switches remain ground-connected.
- Very high PWM frequencies have coarse duty-cycle resolution because of the
  1 MHz timer timebase.
- B1 service authorization protects against accidental remote changes; it is
  not a security boundary.
- Production use requires application-specific electrical protection, EMC,
  timing, fault-injection, and environmental validation.
