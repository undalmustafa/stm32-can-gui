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

- [ ] Preserve the last rejected heartbeat mask across an IWDG reset
- [ ] Expose the previous watchdog failure through diagnostics or event logging
- [ ] Distinguish a health-gate rejection from a hard superloop stall
- [ ] Add host tests for retained-record validation and stale-record rejection

Exit criterion: after an injected watchdog reset, the next boot reports the reset
source and any trustworthy pre-reset health evidence.

### 3. Timing Characterization

- [ ] Capture maximum main-loop, CAN, and RTC check-in intervals under load
- [ ] Exercise maximum RX FIFO traffic, TX congestion, RTC I2C timeouts, and bus-off
  recovery
- [ ] Measure the actual IWDG timeout on representative boards
- [ ] Repeat measurements across the required voltage and temperature range
- [ ] Set timeout and refresh constants from measured bounds and documented margin

Exit criterion: the minimum measured watchdog timeout remains safely above the
maximum valid service interval, with an agreed engineering margin.

### 4. Target Fault Injection

- [ ] Suppress each required heartbeat independently
- [ ] Inhibit refresh and measure reset latency
- [ ] Stall the main loop inside representative services
- [ ] Verify the IWDG reset flag and system boot event after each reset
- [ ] Verify debugger freeze and post-detach behavior
- [ ] Confirm that normal CAN, RTC, and logging faults do not cause false resets

Exit criterion: every intended fault resets and recovers as specified, and the
stress matrix produces no false watchdog resets.

### 5. Production Sign-Off

- [ ] Confirm Release builds contain no fault-injection symbols
- [ ] Review IWDG option bytes and Stop/Standby behavior
- [ ] Document flashing, fault-injection, and acceptance procedures
- [ ] Record tested toolchain, board revision, firmware tag, and measurements

Exit criterion: the signed test record links a tagged firmware artifact to the
hardware and environmental evidence used to approve the watchdog policy.
