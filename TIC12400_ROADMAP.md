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
- The carrier's switches have three physical positions, but measured ADC data
  proves only two electrical states: Left is closed, while Center and Right
  are the same open circuit.
- Explicit debounced `CLOSED`, `OPEN`, and unavailable/diagnostic states.
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

SPI will initially run at 1 MHz for breadboard timing margin, with clock polarity low,
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

- [x] Add SPI3 and GPIO configuration to `can_gui.ioc`.
- [x] Enable the STM32 HAL SPI module and generated-equivalent initialization.
- [x] Configure manual CS, INT EXTI, and active-high RESET output behavior.
- [ ] Verify mode 1, 1 MHz, MSB-first, 32-clock transactions on an oscilloscope
      or logic analyzer.
- [x] Read `DEVICE_ID` and confirm TIC12400-Q1 major/minor ID `0x20`.
- [x] Read and clear the power-on-reset indication.
- [x] Exercise hardware reset and confirm POR/INT behavior.

Exit criterion: 1000 repeated device-ID reads complete without STM32 HAL,
TIC12400 SPI, or parity errors.

### Phase 1 debugger check

Set a breakpoint immediately after `TIC12400_Probe_Init(&hspi3)` in `main.c`,
then inspect `g_tic12400_probe`:

- `online = 1`, `result = TIC12400_RESULT_OK`, and `device_id = 0x20`
  confirm that SPI communication and the device identity are valid.
- `tx_frame = 0x02000000` is the odd-parity `DEVICE_ID` read command.
- Response status bits 31 through 25 are, in order: `POR`, `SPI_FAIL`,
  `PRTY_FAIL`, `SSC`, `VS_TH`, `TEMP`, and `OI`.
- `por_observed = 1` confirms the reset/POR path. `int_status` preserves the
  clear-on-read `INT_STAT` value for inspection.
- If the first response carries a latched SPI/parity fault, the probe preserves
  it in `first_rx_frame`/`first_result`, reads `INT_STAT` to clear the fault,
  and retries `DEVICE_ID`. `recovery_attempted = 1` and
  `recovery_succeeded = 1` identify this recovered startup condition.
- The probe then performs 1000 consecutive `DEVICE_ID` reads. A hardware pass
  has `validation_target = 1000`, `validation_completed = 1000`,
  `validation_passed = 1`, and `validation_first_failure_index = 0`.
- `status.spi_fail = 0`, `status.parity_fail = 0`, and `hal_error = 0` are
  required for a clean transaction.

The probe is intentionally nonfatal: an absent or unpowered TIC12400 records
the failure here without stopping PWM, input capture, CAN, or the watchdog.

### 23-channel binary ADC characterization

After the 1000-read SPI validation passes, the probe performs a small functional
test using every fitted carrier input:

- ADC mode, current-source mode, and continuous monitoring.
- IN0 through IN11 and IN13 through IN23 enabled.
- IN12 permanently disabled by the board mask `0xFFEFFF`.
- 1 mA wetting current selected for every fitted input.
- ADC threshold interrupts intentionally disabled while software filtering is
  accepted.
- Every written register is read back before `TRIGGER` starts monitoring.

This carrier already has 24 onboard slide switches and their input-conditioning
components. Do not add a jumper or external voltage to an IN pin. Operate the
onboard switches directly. The IN12 switch is physically present but its
carrier resistor is not fitted, so firmware intentionally ignores bit 12.

Flash the Debug firmware, run it, and inspect `g_tic12400_probe`:

- `configuration_write_count = 11`, `configuration_completed = 1`,
  `configuration_passed = 1`, `adc_characterization_active = 1`, and
  `monitoring_started = 1`.
- Readbacks must be `config = 0x800`, `mode = 0xFFEFFF`, `cs_select = 0`,
  `wc_cfg0 = 0x249249`, `wc_cfg1 = 0x049249`,
  `in_en = 0xFFEFFF`, `int_en_comp1 = 0`,
  `int_en_comp2 = 0`, and `int_en_cfg0 = 4`.
- `enabled_input_mask = adc_valid_mask = 0xFFEFFF`.
- Before monitoring starts, the probe triggers the TIC12400 hardware
  configuration CRC with `TRIGGER=0`. A pass has
  `crc_trigger_self_cleared = 1`, `crc_completed = 1`,
  `crc_result = TIC12400_RESULT_OK`, and CRC completion bit 8 set in
  `crc_int_status`. `crc_value` preserves the device's 16-bit result.
- `baseline_established = 1`, `service_result = TIC12400_RESULT_OK`, and
  `service_failures = 0`.
- `adc_raw[0]` through `adc_raw[23]` contain the latest 10-bit codes.
  `adc_min` and `adc_max` retain the observed range for each channel.
  `adc_pair_readback[0]` through `[11]` preserve the 12 packed hardware
  registers. IN12 values remain zero because its validity-mask bit is clear.
- The accepted 2026-07-28 capture is stored in
  `docs/tic12400_characterization_20260728.csv`. Left measured code 3 through
  41; Center and Right both measured full-scale code 1023 on all 23 fitted
  channels.
- Firmware classifies code 0 through 512 as `CLOSED` and 513 through 1023 as
  `OPEN`, then requires three consecutive equal classifications.

The main loop performs no SPI work in the ISR and samples every 50 ms during
characterization. Each batch is one `INT_STAT` read plus the 12 packed ADC
register reads. If configuration fails,
`configuration_failure_address`, `configuration_expected`,
`configuration_actual`, and `configuration_result` identify the first failure.
The driver provides 1 us of CS setup and hold margin and holds CS high for 5 us
after every transfer, exceeding the data-sheet 100 ns setup/hold and 1.5 us
inter-frame minima. Runtime SPI failures are retained in the
`first_service_failure_*` and `last_service_failure_*` fields instead of being
hidden when a later transaction succeeds.

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
- [x] Keep `TRIGGER=0` while changing configuration.
- [x] Apply a safe default direct-input profile with IN12 always disabled.
- [x] Read back critical configuration and run the hardware configuration CRC.
- [ ] Independently calculate the configuration CRC in software and compare it
      with the device result.
- [x] Start monitoring only after register readback and hardware CRC completion.
- [x] Treat the first completed detection cycle as the baseline state.
- [x] Handle INT by recording one pending flag in the callback.
- [x] Read `INT_STAT` once per service event and preserve its clear-on-read
      value in software.
- [ ] Read only the dependent status/ADC registers required by that event.
- [x] Limit SPI work per main-loop pass so CAN, RTC, PWM, and watchdog services
      cannot be starved.
- [x] Add bounded raw-ADC characterization for all 23 fitted channels.
- [x] Record accepted Left-closed and Center/Right-open ADC ranges for every
      fitted channel.
- [x] Add three-sample switch-change debounce filtering.
- [ ] Add device-offline detection, controlled reinitialization, and backoff.
- [x] Add sticky counters and last-error information to runtime diagnostics.
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

Implemented monitoring telemetry:

- [x] `0x552` device status every 500 ms: state flags, device ID, service
      result, latest transaction flags, saturated service-failure count, and
      the low 16 bits of the last nonzero clear-on-read `INT_STAT`.
- [x] `0x554` debounced state: 24-bit closed bitmap, 24-bit validity mask,
      generation, and data-valid flag. IN12 is always invalid and clear.
- [x] Transmit state immediately on a stable generation change and every
      500 ms as a replaceable heartbeat.
- [x] Stop cyclic `0x553` raw ADC traffic after characterization; the ID
      remains reserved for engineering diagnostics.
- [ ] Add configuration, action, and channel-detail commands.
- [ ] Add high-priority TIC12400 fault events.

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
- Only the confirmed end-user state: `OPEN`, `CLOSED`, or unavailable.
- Raw ADC, snapshots, generation, SPI details, and characterization controls
  are intentionally excluded from the end-user page.

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

- [x] Add `tic12400_controller.py`.
- [x] Add a read-only `tic12400_panel.py` for debounced OPEN/CLOSED state.
- [x] Compose the page through `main_window_view.py`.
- [x] Route CAN telemetry through the top-level application.
- [x] Add controller, panel, protocol, rendering, bitmap, invalid-data, and
      stale-state tests for the read-only telemetry path.

Exit criterion: the GUI can configure and observe every supported channel
without exposing invalid combinations, and displayed state always comes from
confirmed MCU telemetry rather than optimistic button clicks.

## Phase 6 - ADC, Resistor-Coded Switches, and Low-Power Polling

- [x] Read all 23 fitted raw ADC channels into a coherent firmware snapshot.
- [x] Use a temporary engineering GUI workflow to collect ten complete
      generations per physical position, reject partial/duplicate snapshots,
      display per-channel minimum/maximum codes, and export CSV.
- [x] Capture Left, Center, and Right codes from the actual carrier switches.
- [x] Define the measured binary profile: Left closed, Center/Right open.
- [x] Select a half-scale software threshold with more than 470 ADC codes of
      measured separation on each side.
- [ ] Validate shared threshold dependencies across channels.
- [x] Display only the debounced OPEN/CLOSED switch state to end users.
- [ ] Add continuous/polling mode selection.
- [ ] Configure poll period and active time with range checks.
- [ ] Measure switch detection latency and average supply current.
- [ ] Add clean-current polling only if required by the chosen switch network.
- [ ] Run ADC self-diagnostic and configuration CRC on demand and after
      reconfiguration.

Exit criterion: every resistor-coded position is detected across its tolerance
range, and polling-mode latency/current meet documented targets.

## Phase 7 - Hardware-in-the-Loop and Regression Acceptance

- [ ] Test left/center/right transitions on all 23 fitted channels.
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

## Current Vertical Slice

The SPI, identity, configuration CRC, raw ADC acquisition, binary
characterization, debounce, CAN state telemetry, and simplified end-user GUI
are implemented. The first reviewed control-policy map is also implemented:

- IN0 directly controls LED1.
- IN1 permits GUI-requested PWM operation.
- IN2 permits GUI-requested CAN Slot 1 operation.
- IN3 permits GUI-requested CAN Slot 2 operation.
- Invalid TIC12400 state forces every mapped function off while essential CAN
  transport and diagnostics remain active.

The policy records requested, physical-permission, blocked, and effective
states separately so the GUI never presents an accepted request as an active
output.

Low-power polling and advanced diagnostics follow after the binary
monitoring-and-control path is accepted.
