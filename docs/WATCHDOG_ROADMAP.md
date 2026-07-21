# Watchdog Roadmap

## Objective

The independent watchdog must reset the controller when the cooperative
superloop stops making required progress, while avoiding resets during valid
worst-case CAN, I2C, RTC, and logging activity.

## Current Policy

- Peripheral: STM32 IWDG1, clocked from LSI
- Nominal timeout: 4 seconds
- Health evaluation period: 250 milliseconds
- Required check-ins: main loop, CAN application, and RTC service
- Refresh rule: every required check-in must be observed in the current health
  window
- Debug behavior: IWDG1 is frozen while the core is halted by a debugger
- Release behavior: fault-injection setters are not compiled
- Hardware ownership: CubeMX-generated `hiwdg1` configuration in `main.c`
- Policy ownership: `app_watchdog` binds to `hiwdg1` and controls refreshes
- Reset evidence: a CRC-protected, commit-marked record in backup SRAM is
  accepted only when RCC independently reports an IWDG reset

The timeout uses a prescaler of 128 and reload value of 999. Its real duration
depends on the physical LSI frequency and must be measured on target hardware.

## Completed

### Foundation

- [x] Add the health-gated IWDG module
- [x] Integrate main-loop, CAN, and RTC check-ins
- [x] Capture and decode RCC reset flags before clearing them
- [x] Record reset cause in the system boot event
- [x] Provide debugger-visible watchdog diagnostics

### Automated Verification

- [x] Add portable Debug and Release firmware builds
- [x] Add host tests for initialization and refresh gating
- [x] Test missing-heartbeat rejection and recovery
- [x] Test refresh errors, fault injection, and tick wraparound
- [x] Run watchdog tests, firmware builds, and GUI tests in CI
- [x] Publish verified firmware and GUI artifacts from version tags

### Software Hardening

- [x] Exclude watchdog fault-injection APIs from Release builds
- [x] Count every service check-in
- [x] Track the maximum observed interval for each required check-in

## Next Work

### 1. CubeMX Ownership

- [x] Represent the IWDG peripheral and its parameters in `can_gui.ioc`
- [x] Make CubeMX the sole owner of peripheral initialization and `hiwdg1`
- [x] Bind the application health policy to the generated handle
- [x] Regenerate code in a temporary tree and review the generated delta
- [x] Confirm that repeated CubeMX generation preserves watchdog integration

Exit criterion: CubeMX regeneration followed by both firmware builds produces no
manual repair and no duplicate IWDG initialization.

### 2. Reset Evidence and Telemetry

- [x] Preserve the last rejected heartbeat mask across an IWDG reset
- [x] Expose the previous watchdog failure through diagnostics or event logging
- [x] Distinguish a health-gate rejection from a hard superloop stall
- [x] Add host tests for retained-record validation and stale-record rejection
- [x] Verify the complete retained-evidence path after an on-target IWDG reset

Exit criterion: after an injected watchdog reset, the next boot reports the reset
source and any trustworthy pre-reset health evidence.

Target result (2026-07-21): on NUCLEO-H7A3ZI-Q Debug firmware, suppressing the
RTC heartbeat reset the MCU and the next boot reported valid retained evidence
with health-gate rejection cause `2` and missing-heartbeat mask `4`.

### 3. Timing Characterization

- [x] Capture maximum main-loop, CAN, and RTC check-in intervals under load
- [x] Exercise sustained RX command traffic with TX congestion
- [x] Exercise RTC disconnection and I2C NACK handling under load
- [ ] Exercise a stuck-bus I2C timeout under load
- [x] Exercise CAN bus-off recovery under load
- [ ] Measure the actual IWDG timeout on representative boards
- [ ] Repeat measurements across the required voltage and temperature range
- [ ] Set timeout and refresh constants from measured bounds and documented margin

Exit criterion: the minimum measured watchdog timeout remains safely above the
maximum valid service interval, with an agreed engineering margin.

Baseline target result (2026-07-21): after `153359 ms` of normal Debug runtime,
the maximum observed main-loop, CAN-application, and RTC-service heartbeat
intervals were all `2 ms`. Loaded traffic and fault scenarios remain to be
measured.

TX-load target result (2026-07-21): two standard CAN slots with distinct IDs
ran concurrently at `1 ms` periods for two minutes. No watchdog reset occurred,
and the maximum observed heartbeat interval remained `2 ms` for the main loop,
CAN application, and RTC service.

Combined CAN-load target result (2026-07-21): while both `1 ms` transmit slots
were active, the PCAN stress runner sent `117911` valid commands in `120.210 s`
(`980.9` frames/s) and received `123905` frames (`1030.7` frames/s). The host
missed `2090` scheduling periods. Target diagnostics at `190520 ms` showed one
initialization, `762` successful refreshes, no refresh errors or health-gate
rejections, and `2 ms` maximum intervals for all three heartbeats.

RTC-fault target result (2026-07-21): with the PCA2131 disconnected during CAN
load, the RTC reported not ready, `PCA2131_RESULT_READ_FAILED`, `HAL_ERROR`, and
I2C error `4` (acknowledge failure). At `81770 ms`, the watchdog showed one
initialization, `327` successful refreshes, no refresh errors or health-gate
rejections, and `1 ms` maximum intervals for all three heartbeats.

Bus-off target result (2026-07-21): an injected physical CAN-bus fault during
cyclic traffic produced `60` bus-off events and `32` recovery attempts. There
were no HAL recovery failures; `30` verification attempts were invalidated by
another bus-off while the fault remained active. After the fault was removed,
two recoveries were confirmed by TX-complete events and verification was no
longer pending. The watchdog retained one initialization with no health-gate
rejection or refresh error. Removing the sole ACK-capable node is suitable for
testing the error-passive path, but CAN fault-confinement rules prevent repeated
ACK errors alone from driving a lone transmitter to bus-off. Future bus-off
tests require controlled fault-injection hardware that creates bit, stuff, or
form errors; CANH/CANL must not be manually shorted.

### 4. Target Fault Injection

- [x] Suppress each required heartbeat independently
- [x] Inhibit refresh and confirm hard-stall retained evidence
- [x] Measure watchdog reset latency
- [ ] Stall the main loop inside representative services
- [ ] Verify the IWDG reset flag and system boot event after each reset
- [x] Verify debugger freeze while the core is halted
- [ ] Verify watchdog operation after debugger detach
- [ ] Confirm that normal CAN, RTC, and logging faults do not cause false resets

Exit criterion: every intended fault resets and recovers as specified, and the
stress matrix produces no false watchdog resets.

Target result (2026-07-21): Debug fault injection independently suppressed the
main-loop, CAN-application, and RTC-service heartbeat masks (`1`, `2`, and `4`).
Each reset produced valid health-gate evidence with the corresponding missing
mask. Inhibiting refresh produced valid hard-stall evidence with cause `1` and
no missing heartbeat. Halting the core for more than 10 seconds did not reset
the MCU, and watchdog refresh resumed after the debugger continued execution.
Three manual inhibit-to-reset measurements were `4.17 s`, `3.80 s`, and
`4.18 s` (mean `4.05 s`). These stopwatch results are preliminary; instrumented
LSI and timeout measurements remain part of timing characterization.

### 5. Production Sign-Off

- [x] Confirm Release builds contain no fault-injection symbols
- [ ] Review IWDG option bytes and Stop/Standby behavior
- [ ] Document flashing, fault-injection, and acceptance procedures
- [ ] Record tested toolchain, board revision, firmware tag, and measurements

Exit criterion: the signed test record links a tagged firmware artifact to the
hardware and environmental evidence used to approve the watchdog policy.

Build result (2026-07-21): linked Debug firmware retains the debugger-only
watchdog fault-injection hooks, while linked Release firmware contains none.
The CI firmware job audits both symbol sets after linking.
