# TIC12400-Q1 Switch Monitoring Roadmap

## Goal

Integrate a Texas Instruments TIC12400-Q1 24-channel Multiple Switch Detection
Interface (MSDI) with the NUCLEO-H7A3ZI-Q firmware and expose its live state,
configuration, interrupts, and diagnostics on a dedicated GUI page over the
existing reliable Classic CAN protocol.

The integration must preserve the working FDCAN, PCA2131 RTC, PWM, input
capture, watchdog, and retained event-log features.

## What the TIC12400-Q1 Does

The TIC12400-Q1 is an automotive switch-monitor front end, not a general-purpose
GPIO expander. It sits between 12 V mechanical switches and a low-voltage MCU.

- Monitors 24 direct switch inputs.
- IN0 through IN9 can monitor switches connected to ground or battery.
- IN10 through IN23 support ground-connected switches.
- Every input can use comparator or 10-bit ADC measurement.
- ADC mode supports resistor-coded, multi-position switches.
- Programmable wetting current settings are 0, 1, 2, 5, 10, and 15 mA.
- Continuous mode keeps wetting current active.
- Polling mode reduces average system current.
- An active-low, open-drain interrupt reports switch and diagnostic events.
- Integrated diagnostics cover SPI/parity, supply, temperature, configuration
  CRC, ADC self-test, and selected wetting-current paths.
- Matrix and input-sharing modes can monitor more switches, but they add
  configuration and validation complexity and are not part of the first
  milestone.

The device communicates with a 32-bit, MSB-first SPI frame. It uses software
odd parity, returns status flags during every transaction, and supports SCLK up
to 4 MHz. SCLK must be low when chip select changes.

The current Rev. C data sheet is authoritative for the SPI implementation. Some
older EVM material describes the register communication as 24-bit; the current
device transaction still requires exactly 32 SCLK pulses including command,
address, status/data, and parity fields.

Official references:

- [TIC12400-Q1 data sheet](https://www.ti.com/lit/ds/symlink/tic12400-q1.pdf)
- [TIC12400-Q1 product page](https://www.ti.com/product/TIC12400-Q1)
- [TIC12400 EVM user's guide](https://www.ti.com/lit/pdf/scpu036)
- [NUCLEO-H7A3ZI-Q user manual](https://www.st.com/resource/en/user_manual/um2408-stm32h7-nucleo144-boards-mb1363-stmicroelectronics.pdf)

## Initial Scope

The first usable release will support:

- One TIC12400-Q1 device.
- The 23 fitted direct inputs: IN0 through IN11 and IN13 through IN23.
- IN12 permanently unavailable because its required carrier-board resistor is
  not fitted.
- Comparator mode for ordinary open/closed switches.
- ADC mode and raw ADC readback for resistor-coded switches.
- Ground-connected inputs on every fitted channel.
- Battery-connected inputs only on IN0 through IN9.
- Per-channel enable, input type, measurement mode, wetting current, threshold
  selection, and interrupt-edge configuration.
- Continuous operation first, followed by low-power polling.
- Interrupt-driven updates with a bounded periodic fallback poll.
- Device, switch, ADC, supply, temperature, SPI, parity, and configuration
  diagnostics.
- Configuration through the existing physical service-access authorization.

Deferred until the direct-input implementation is accepted:

- 4x4, 5x5, and 6x6 matrix modes.
- Input-sharing topologies.
- MCU sleep/wake power-control circuitry.
- Multiple TIC12400-Q1 devices.
- A general-purpose raw register editor in the main GUI.

## Proposed Hardware Interface

The SPI1 MISO option on PA6 conflicts with TIM3 input capture, so the interface
uses SPI3 on otherwise unused STM32 GPIOs exposed by the NUCLEO-H7A3ZI-Q
connectors. PC10, PC11, and PC12 avoid the PB3/SWO solder-bridge routing:

| TIC12400-Q1 signal | STM32 pin | Nucleo connector | Configuration |
|---|---|---|---|
| SI / MOSI | PC12 | CN8 pin 10 | SPI3 MOSI |
| SCLK | PC10 | CN8 pin 6 | SPI3 SCK |
| CS | PA4 | CN7 pin 17 | Software-controlled GPIO output |
| SO / MISO | PC11 | CN8 pin 8 | SPI3 MISO |
| INT | PG6 | CN10 pin 13 | GPIO EXTI, falling edge, 3.3 V pull-up |
| RESET | PA2 | CN10 pin 11 | GPIO output, low during normal operation |
| VDD | - | Nucleo 3V3 rail | 3.0-5.5 V allowed; project uses 3.3 V |
| DGND/AGND | - | Nucleo GND | Common ground |
| VS | - | External protected supply | 4.5-35 V allowed; begin testing at 12 V |

This SPI3 route leaves normal SWD debugging on PA13/PA14 and SWO on PB3
untouched. The alternative PB3/PB4/PB5 SPI3 route is not used because exposing
PB3 as SCLK exclusively on CN7 requires Nucleo solder-bridge configuration
SB49 ON and SB50 OFF.

The selected VDD is 3.3 V because the STM32H7 GPIO operates at 3.3 V. VDD must
be present during every SPI transaction. VS is a separate protected device
supply within the 4.5-35 V operating range; it must not be connected to the
Nucleo 3.3 V rail. The initial 12 V bench point represents the intended
automotive use case, not the only supported VS voltage.

SPI will initially run at 2 MHz for timing margin, with clock polarity low,
second-edge sampling (SPI mode 1), MSB first, software chip select, and exactly
32 clocks per transaction.

Before implementation, confirm whether the hardware is a TI EVM, a third-party
module, or a custom board. The carrier must provide the data-sheet decoupling,
CS pull-up, INT pull-up, supply protection, and input protection appropriate to
the intended test environment. The final wiring table must follow the actual
carrier schematic.

## Target Architecture

```text
Mechanical / resistor-coded switches
                |
                v
        TIC12400-Q1 inputs
                |
        comparator / 10-bit ADC
                |
       SPI3 + INT + RESET
                |
                v
  tic12400 driver -> tic12400_app state machine
                         |
          diagnostics + retained event log
                         |
                generated CAN protocol
                         |
                         v
               TIC12400 GUI page
```

### Firmware layers

`tic12400.c/h`

- Register addresses and bit masks.
- 32-bit read/write frame encoding.
- Odd-parity generation and validation.
- SPI status-flag decoding.
- Device-ID check.
- Hardware/software reset support.
- Register read/write and optional readback verification.
- HAL errors, parity errors, SPI errors, and transaction counters.

`tic12400_app.c/h`

- Non-blocking initialization and configuration state machine.
- Validated configuration model instead of arbitrary register writes.
- Board capability mask that permanently prevents IN12 from being enabled.
- Interrupt-pending flag set by EXTI; all SPI work remains in main-loop context.
- Bounded SPI transaction budget per `Process()` call.
- Switch, ADC, interrupt, supply, temperature, and fault snapshots.
- Periodic fallback poll in case an interrupt edge is missed.
- CAN command handling and telemetry scheduling.
- Runtime diagnostics and retained event-log integration.

### GUI layers

`tic12400_controller.py`

- Encode configuration/action commands.
- Decode device, bitmap, channel-detail, and diagnostic telemetry.
- Track desired, pending, confirmed, stale, and fault states.

`tic12400_panel.py`

- Dedicated scrollable TIC12400 page.
- Device overview and connection state.
- 24-channel live-state grid.
- Per-channel editor.
- Polling and device controls.
- Diagnostics and recent interrupt/fault view.

## Phase 0 - Hardware and Use-Case Contract

- [ ] Identify the exact module/EVM and revision.
- [ ] Obtain its schematic and connector pinout.
- [ ] Confirm VS is within 4.5-35 V, VDD is within 3.0-5.5 V, and verify
      ground, protection, and decoupling.
- [ ] Confirm which inputs will use ground switches, battery switches, and
      resistor-coded switches.
- [ ] Confirm from the carrier schematic that IN12 is not fitted and record it
      in the board capability mask.
- [ ] Confirm the provisional SPI3, INT, and RESET pins on the physical board.
- [ ] Define a safe bench test circuit and maximum applied voltage.
- [ ] Record the initial per-channel profile and expected switch states.

Exit criterion: reviewed wiring table and channel profile with no pin, voltage,
or switch-topology ambiguity.

## Phase 1 - SPI and Device Bring-Up

- [ ] Add SPI3 and GPIO configuration to `can_gui.ioc`.
- [ ] Enable the STM32 HAL SPI module and generated initialization.
- [ ] Configure manual CS, INT EXTI, and active-high RESET output behavior.
- [ ] Verify mode 1, 2 MHz, MSB-first, 32-clock transactions on an oscilloscope
      or logic analyzer.
- [ ] Read `DEVICE_ID` and confirm TIC12400-Q1 major/minor ID `0x20`.
- [ ] Read and clear the power-on-reset indication.
- [ ] Exercise hardware reset and confirm POR/INT behavior.

Exit criterion: 1000 repeated device-ID reads complete without STM32 HAL,
TIC12400 SPI, or parity errors.

## Phase 2 - Production-Quality TIC12400 Driver

- [ ] Implement register definitions without magic numbers.
- [ ] Implement 32-bit command packing and response unpacking.
- [ ] Implement transmit and receive odd-parity validation.
- [ ] Decode POR, SPI failure, parity failure, switch-change, supply,
      temperature, and other-interrupt flags from every transaction.
- [ ] Validate register addresses, reserved bits, and writable masks.
- [ ] Add bounded HAL timeouts and explicit error results.
- [ ] Add reset, device-ID, status, configuration CRC, and diagnostic helpers.
- [ ] Add a mockable SPI/GPIO boundary for host tests.
- [ ] Test successful reads/writes, previous-value write response, parity
      failures, invalid addresses, HAL failures, and resets.

Exit criterion: driver host tests pass with branch coverage for all error
results, and bench reads match a logic-analyzer decode.

## Phase 3 - Device Service and Direct-Input Monitoring

- [ ] Implement a non-blocking initialization sequence.
- [ ] Keep `TRIGGER=0` while changing configuration.
- [ ] Apply a safe default direct-input profile with IN12 always disabled.
- [ ] Read back critical configuration and calculate/compare configuration CRC.
- [ ] Start monitoring only after the configuration is verified.
- [ ] Treat the first completed detection cycle as the baseline state.
- [ ] Handle INT by recording one pending flag in the callback.
- [ ] Read `INT_STAT` once per service event and preserve its clear-on-read
      value in software.
- [ ] Read only the dependent status/ADC registers required by that event.
- [ ] Limit SPI work per main-loop pass so CAN, RTC, PWM, and watchdog services
      cannot be starved.
- [ ] Add switch-change debounce/filter configuration.
- [ ] Add device-offline detection, controlled reinitialization, and backoff.
- [ ] Add sticky counters and last-error information to runtime diagnostics.
- [ ] Add retained log events for initialization, reset, configuration failure,
      SPI/parity failure, supply/temperature faults, and switch changes selected
      for logging.

Exit criterion: all 23 fitted direct channels update correctly, IN12 remains
disabled, the existing application is not blocked, and a disconnected/reset
TIC12400 recovers predictably.

## Phase 4 - CAN Protocol

All protocol definitions must first be appended to
`protocol/can_protocol.yaml`, then regenerated for C and Python.

Proposed command groups:

- Device configuration: operating mode, polling timing, interrupt behavior.
- Channel configuration: channel, enable, switch type, comparator/ADC mode,
  wetting current, threshold selection, and interrupt edge.
- Device action: initialize, apply, start, stop, reset, CRC test, ADC self-test.
- Detail request: request the latest complete state for one channel.

Proposed telemetry groups:

- Device status and health.
- 24-bit switch-state bitmap plus an availability mask and
  validity/generation information. IN12 remains zero and unavailable so the
  wire layout stays aligned with the TIC12400 register layout.
- Interrupt/fault summary.
- Requested channel detail with raw 10-bit ADC value and interpreted state.
- Configuration/action result.

Protocol rules:

- Do not send one cyclic CAN frame per channel.
- Send compact bitmaps periodically and detailed channel data on change or
  request.
- Coalesce replaceable periodic status frames.
- Send fault and selected switch-change events at high priority.
- Include a snapshot generation counter so related frames can be correlated.
- Put all configuration/reset/start actions behind the existing B1 physical
  service-access window.
- Keep status/detail reads and safe stop actions available while access is
  locked.
- Use the existing reliable command session and ACK behavior.

Exit criterion: generated C and Python constants have no drift, codec tests
cover every payload, and worst-case TIC12400 telemetry stays within the current
Classic CAN transport budget.

## Phase 5 - Dedicated GUI Page

### Device overview

- Online/offline/initializing/fault state.
- Device ID and firmware-side driver state.
- Continuous or polling mode and monitoring state.
- Latest update age and snapshot generation.
- INT, POR, SPI/parity, supply, temperature, CRC, ADC, and wetting-current
  diagnostic indicators.

### 24-position channel grid

Each channel shows:

- IN0 through IN23.
- IN12 shown as `Not fitted`, unavailable, and not configurable.
- Enabled/disabled.
- Ground or battery switch type.
- Comparator or ADC mode.
- Open/closed or interpreted position.
- Raw ADC code when applicable.
- Wetting current.
- Threshold/profile.
- Interrupt edge and recent-change indication.

### Configuration

- Select a channel and edit only settings valid for that channel.
- Reject every configuration request targeting IN12 in both GUI and firmware.
- Prevent battery-switch selection on IN10 through IN23.
- Validate dependent/shared thresholds before sending.
- Show desired, pending, ACKed, and readback-confirmed configuration
  separately.
- Require service access for apply, reset, self-test, and start actions.
- Provide explicit start, stop, reinitialize, CRC-test, and ADC-test controls.

### Diagnostics

- SPI transaction and HAL-error counters.
- Parity and device SPI-error counters.
- Interrupt and missed/fallback-poll counters.
- Reset/reinitialization count.
- Last INT status and decoded fault.
- Recent TIC12400 events integrated with the existing Logs & Errors page.

- [ ] Add `tic12400_controller.py`.
- [ ] Add `tic12400_panel.py`.
- [ ] Compose the page through `main_window_view.py`.
- [ ] Route CAN telemetry through the top-level application.
- [ ] Add controller, panel, protocol, rendering, and stale-state tests.

Exit criterion: the GUI can configure and observe every supported channel
without exposing invalid combinations, and displayed state always comes from
confirmed MCU telemetry rather than optimistic button clicks.

## Phase 6 - ADC, Resistor-Coded Switches, and Low-Power Polling

- [ ] Define named resistor-ladder profiles and expected ADC ranges.
- [ ] Calculate thresholds with component tolerance, wetting-current tolerance,
      ground shift, and ADC margin.
- [ ] Validate shared threshold dependencies across channels.
- [ ] Display raw ADC code and interpreted switch position.
- [ ] Add continuous/polling mode selection.
- [ ] Configure poll period and active time with range checks.
- [ ] Measure switch detection latency and average supply current.
- [ ] Add clean-current polling only if required by the chosen switch network.
- [ ] Run ADC self-diagnostic and configuration CRC on demand and after
      reconfiguration.

Exit criterion: every resistor-coded position is detected across its tolerance
range, and polling-mode latency/current meet documented targets.

## Phase 7 - Hardware-in-the-Loop and Regression Acceptance

- [ ] Test open/closed transitions on all 23 fitted channels.
- [ ] Test ground-connected switches on all fitted channels.
- [ ] Confirm IN12 cannot be enabled through firmware or GUI commands and
      never sources or sinks wetting current.
- [ ] Test battery-connected switches only on IN0 through IN9.
- [ ] Test switch bounce and configured detection filters.
- [ ] Test simultaneous and rapid multi-channel changes.
- [ ] Test ADC thresholds with a resistor decade box or characterized ladder.
- [ ] Confirm INT assertion and clear-on-read behavior with a logic analyzer.
- [ ] Reset and disconnect the TIC12400 while the rest of the application runs.
- [ ] Inject safe SPI/parity failures where practical.
- [ ] Exercise under-voltage, over-voltage, and temperature diagnostics only
      with suitable protected laboratory equipment.
- [ ] Stress CAN while TIC12400 interrupts are active.
- [ ] Confirm no false application-watchdog resets.
- [ ] Re-run RTC, alarm, PWM, input-capture, loopback, CAN recovery, retained
      log, host C, and Python GUI tests.
- [ ] Record board/module revisions, wiring, firmware commit, instruments,
      supply conditions, and measured results.

Exit criterion: signed test evidence demonstrates correct switch detection,
fault reporting, recovery, CAN behavior, and no regressions in existing
features.

## Recommended First Milestone

Do not begin with all device features at once. The first vertical slice should
be:

1. Confirm the exact module and wiring.
2. Bring up SPI3 and read `DEVICE_ID`.
3. Configure one ground-connected comparator input in continuous mode.
4. Observe one switch transition through INT and firmware diagnostics.
5. Publish that one channel over CAN.
6. Display it on a minimal TIC12400 GUI page.

After that path works end to end, expand the same tested architecture to all 23
fitted channels, ADC profiles, low-power polling, and advanced diagnostics.
