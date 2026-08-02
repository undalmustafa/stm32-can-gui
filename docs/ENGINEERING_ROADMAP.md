# Firmware Engineering Roadmap

Updated: 2026-08-02

This roadmap is the verified form of the findings originally collected in
`/home/musti/Documents/plan.txt`. It separates software completion from
hardware acceptance and keeps product development ahead of lower-priority
maintenance work.

## Prioritization rule

Risk is ordered by:

`priority = safety impact x likelihood x diagnostic difficulty`

A phase is not considered fully accepted merely because code exists. Host
tests, target compilation, and relevant hardware acceptance tests answer
different questions and are tracked separately.

## Phase 0 — Clock accuracy and crash evidence

Status: software complete; target acceptance pending.

### 0.1 Externally referenced CAN clock

- [x] Move the PLL source from HSI to the Nucleo ST-LINK 8 MHz MCO on
  `PH0/OSC_IN` using HSE bypass.
- [x] Keep PLL input near 2.667 MHz, VCO at 256 MHz, SYSCLK at 64 MHz, and
  PLL1Q/FDCAN at 32 MHz.
- [x] Preserve Classic CAN at 500 kbit/s and an 81.25% sample point.
- [x] Protect clock and bit-timing relationships with `_Static_assert`.
- [x] Keep `can_gui.ioc` aligned with the source configuration.
- [ ] Measure the 8 MHz MCO and 2 us CAN bit time on target.
- [ ] Complete cold/hot startup and long-bus error-counter testing.

### 0.2 Retained CPU fault record

- [x] Capture NMI, HardFault, MemManage, BusFault, and UsageFault with correct
  MSP/PSP frame selection.
- [x] Persist R0-R3, R12, LR, PC, xPSR, CFSR, HFSR, DFSR, AFSR, MMFAR, BFAR,
  SHCSR, and ICSR in CRC-protected retained RAM.
- [x] Handle floating-point extended frames and stacking faults separately.
- [x] Make `Error_Handler()` persist the call site and reset the MCU.
- [x] Consume the record once after reboot and copy essential evidence to the
  retained event log.
- [x] Cover valid, corrupt, single-consumption, and invalid-fault records in
  host tests.
- [ ] Inject controlled UsageFault/BusFault cases and resolve the recorded PC
  against the ELF/map on target.

## Phase 1 — Fail-safe startup and output safety

Status: software slice complete; target fault injection pending. A TIM1/TIM8
hardware-break migration remains a hardware change.

### 1.1 Startup state machine

- [x] Establish safe GPIO levels and start IWDG immediately after clock setup.
- [x] Convert FDCAN, I2C, SPI, TIM2, and TIM3 initialization to detailed result
  interfaces.
- [x] Track attempted, ready, failed, and degraded resource masks and the first
  failed stage.
- [x] Do not start PWM/capture or driver transactions when dependencies are
  unavailable.
- [x] Treat optional COM and peripheral failures as degraded rather than
  fatal; expose them through the yellow LED and retained event log.
- [x] Keep IWDG mandatory; an IWDG initialization failure remains fatal.
- [ ] Disconnect each peripheral independently and verify no reset loop, no
  unsafe output, and correct CAN health/status evidence.

### 1.2 PWM safe state

- [x] Make every software fault/reset path disable PWM first.
- [x] Add a HAL-independent emergency path that stops TIM2 CH1 and drives PA0
  low.
- [x] Use it for CPU faults, `Error_Handler`, TIM2 init failure, and PWM stop
  failure.
- [x] Reject the PWM built-in test when PWM or capture is unavailable.
- [ ] Recalculate the watchdog window from measured WCET and recovery data.
- [ ] Measure PA0 safe-state latency during stalls, initialization faults, and
  HardFault.
- [ ] For production hardware, migrate to TIM1/TIM8 BRK with an external fault
  line if required by the safety concept.

## Phase 2 — CAN receive robustness and timing evidence

Status: software complete; HIL acceptance pending.

### 2.1 CAN RX

- [x] Increase RX FIFO0 from 3 to 32 elements within the SRAMCAN budget.
- [x] Use blocking FIFO behavior so saturation preserves older commands.
- [x] Enable new-message, 24-element watermark, full, and message-lost
  notifications.
- [x] Keep ISR work bounded to flags, fill-level sampling, and counters; parse
  and execute in the main loop.
- [x] Expose lost/full/watermark counters through `0x560`, GUI health,
  diagnostics, and coalesced event records.
- [x] Cover 32-frame bursts and saturation counters in host tests.
- [ ] Run 500 kbit/s burst, overflow, and bus-off-during-RX HIL tests.

### 2.2 WCET and latency measurement

- [x] Use DWT CYCCNT for current/min/max/budget/overrun data for the main loop
  and eight services, including counter wrap.
- [x] Budget TIC12400 and PCA2131 blocking operations at 12 ms, CAN at 25 ms,
  and the complete loop at 50 ms.
- [x] Measure RX-to-ACK-enqueue latency with nine buckets and approximate
  p50/p95/p99/max.
- [x] Publish service timing on `0x561` and ACK timing on `0x562`.
- [x] Render bounded histories, sparklines, and event decoding in the GUI.
- [ ] Record at least ten minutes of normal and injected-fault target timing.
- [ ] Send at least 10,000 valid/invalid commands and archive latency evidence
  with commit, clock, bus load, and duration metadata.

### 2.3 NVIC priority contract

- [x] Define `NVIC_PRIORITYGROUP_4`: safety timer 0, SysTick 1, FDCAN 2,
  TIC12400 EXTI 3, future application timers 4-14, and user button 15.
- [x] Keep `app_irq_policy.h`, HAL configuration, source initialization, and
  `can_gui.ioc` consistent.
- [x] Document that ISRs may only perform bounded flag/counter/status copies;
  blocking HAL, logging, transport, and state-machine work is forbidden.
- [x] Reject invalid priority relationships in host tests.
- [ ] Audit target NVIC registers and run simultaneous CAN/TIC12400 interrupt
  storms.

See `docs/INTERRUPT_POLICY.md` for the complete table and HIL checklist.

## Phase 3 — Test and static-quality gates

Status: host state-machine coverage and current CI quality gates are complete.
License work is intentionally deferred.

- [x] Test CAN bus-off recovery, rate limiting, phase resume, notification
  restore, TX-complete verification, stale verification rejection, event
  coalescing, and tick wrap with HAL stubs.
- [x] Test the real FDCAN startup orchestration across peripheral, filter,
  global-filter, FIFO, watermark, start, and notification failures.
- [x] Separate B1 access request/update/query semantics and cover expiry and
  tick wrap.
- [x] Test production RX orchestration, frame gates, ACK results, and the
  eight-frame processing budget.
- [x] Test RTC init/reconnect, date-time/alarm write/readback, mismatch,
  retries, reinitialization, and tick wrap.
- [x] Test TIC12400 identity, configuration, CRC, interrupt/fallback service,
  offline recovery, and profile reconfiguration.
- [x] Enforce `-Wextra -Wconversion -Wshadow -Wundef -Werror` on project-owned
  firmware and host sources. Build vendored ST sources separately with
  `-Wall -Wextra`.
- [x] Gate Cppcheck error/warning/performance/portability findings and retain
  report artifacts.
- [x] Gate nested-IRQ worst-case stack usage from `.su` plus disassembly and
  reserve 2 KiB in linker/CubeMX contracts.
- [x] Machine-check API call-context declarations and ISR seam naming.
- [x] Consolidate Python CI on Python 3.13 with Make-based syntax/regression
  execution.
- [x] Lock direct and transitive Python dependencies.
- [ ] Add the root project license and third-party notices after the product
  update path is complete.

## Phase 4 — Maintenance backlog

- [x] Split the side-effecting control-access query into request, update, and
  read-only operations.
- [ ] Add B1 debounce.
- [ ] Document the cache/MPU and 64 MHz operating decision using measurements.
- [ ] Compare hardware CRC against the current software CRC using measured
  performance and dependency cost.

## Product roadmap

Product development runs before the remaining license, HIL, and maintenance
items. Existing host quality gates remain mandatory. Files without runtime,
build, test, release, or verifiable engineering value are removed after a
reference audit.

### 1. Remove bring-up and debug-only paths

- [x] Remove the 1,000-iteration TIC12400 SPI probe and counters; retain one
  identity read, register readback, and configuration CRC.
- [x] Remove watchdog GDB fault hooks, debug linker sections, and the HIL stress
  utility while retaining isolated host fault-gate tests.
- [x] Remove unused raw TIC12400 ADC telemetry, software debounce,
  characterization state, and engineering frame `0x553` from firmware,
  protocol, DBC, and GUI.
- [x] Hide module-internal diagnostic state and remove obsolete sub-roadmaps
  and characterization artifacts.
- [x] Remove unused newlib syscall/heap templates and unused HSEM build input.

### 2. Generate DBC from `can_protocol.yaml`

- [x] Generate frame IDs, direction, DLC, descriptions, extended-ID flags,
  command value tables, and deterministic artifacts.
- [x] Define real command ACK, TIC12400, RTC, policy, PWM/capture, built-in
  test, log, RX-health, and timing signals.
- [x] Generate firmware, Python, and DBC constants from one schema.
- [x] Model GUI command payloads as complete multiplexed signals and remove
  generic payload-byte fallbacks.

### 3. ISO-TP and UDS diagnostics

- [x] Add a HAL-independent Classic CAN ISO-TP core with caller-owned static
  buffers, multi-frame flow control, sequence wrap, WAIT limits, STmin,
  N_Bs/N_Cr timeout, and tick-wrap tests.
- [x] Bind physical IDs `0x7E0`/`0x7E8` to the shared RX budget and
  high-priority transport queue; fail closed on backpressure.
- [x] Add UDS `0x10`, `0x22`, and `0x3E`, sessions, suppression, S3 timeout,
  NRCs, multi-DID read, and F100-F103 evidence.
- [x] Add a non-blocking Python ISO-TP/UDS client without a second bus reader
  and render live diagnostics.

### 4. Signed A/B CAN bootloader

- [x] Partition 2 MiB flash into bootloader, 896 KiB A/B slots, and redundant
  boot-control storage.
- [x] Freeze the 128-byte signed manifest and 1 KiB vector header contract.
- [x] Validate slot bounds, MSP/reset vector, monotonic security counter,
  SHA-256 digest, and signature callbacks fail-closed.
- [x] Add CRC/generation-protected redundant state, pending attempts,
  confirmation, and rollback policy.
- [x] Add boot target orchestration, A/B linker profiles, vector relocation,
  and safe Cortex-M7 application handoff.
- [x] Add the power-loss-safe STM32 erase/program/read backend.
- [x] Add UDS programming session, inactive-slot erase, RequestDownload,
  TransferData, TransferExit, sequence/retry behavior, and host tests.
- [x] Add the desktop Flash page and sequential multi-frame workflow; reject
  malformed, unsigned, or wrong-slot artifacts before sending data.
- [x] Run the complete hardware-free C and offscreen GUI regression suites on
  2026-08-02.
- [ ] Connect the STM32 slot writer and embedded signature verifier to the UDS
  finalizer, then persist the image as pending.
- [ ] Add offline signing, embedded public-key verification, image packaging,
  and signed release artifacts.
- [ ] Complete target erase/program/reset/confirmation/rollback acceptance.

### 5. Classic CAN to CAN FD migration

- [ ] Define compatibility/versioning policy before expanding payloads from 8
  to 64 bytes.
- [ ] Add nominal 500 kbit/s and data-phase timing with target measurements.
- [ ] Regenerate firmware, Python, and DBC contracts and preserve UDS behavior.

### 6. Authenticated commands

- [ ] Define a persistent freshness-counter lifecycle.
- [ ] Add CMAC-based command authentication and key provisioning.
- [ ] Replace the B1 maintenance window as a security claim; retain physical
  authorization where useful for service safety.

### 7. Nightly HIL regression

- [ ] Add a self-hosted runner with ST-LINK and a second CAN node.
- [ ] Flash signed images and exercise bus-off, TIC12400 disconnect, watchdog
  stall, PWM loopback, update rollback, and evidence collection nightly.

## Current next step

The immediate product task is the firmware-update finalizer: validate the
downloaded inactive-slot payload with an embedded public key, write the held
header last, verify readback, schedule the slot as pending, and persist the
boot-control record. The signing/release toolchain follows that integration.
