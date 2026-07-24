# PWM Control & Input Capture — Roadmap

## Overview

This roadmap covers GUI control of the PWM output (TIM2 CH1 on PA0), input
capture measurement of frequency and duty cycle, and the closed-loop built-in
test that joins PWM, capture, and CAN telemetry.

---

## Current State

| Item | Status |
|---|---|
| PWM output driver (`pwm_control.c`) | ✅ Working — TIM2 CH1, PA0, 1 MHz clock |
| Default output | ✅ 10 kHz, 90% duty cycle |
| GUI control | ✅ PWM & Capture panel with live telemetry |
| Input capture | ✅ TIM3 PWM input on PA6 |
| Built-in test | ✅ MCU-managed 10-point closed-loop sequence |
| CAN protocol definitions | ✅ Commands and telemetry generated from YAML |

**Other available timers**: TIM1, TIM4, TIM5, TIM8, TIM12–TIM17

---

## Architecture

```text
Desktop GUI                          STM32H7A3 Firmware
┌─────────────────────┐              ┌──────────────────────────────┐
│  PWM Control Panel  │              │                              │
│  ┌───────────────┐  │   0x40       │  can_app.c                   │
│  │ Frequency     │──┼──CMD────────►│    ├─► PWM_Control_Set()     │
│  │ Duty Cycle    │  │              │    │     (TIM2 CH1, PA0)     │
│  │ Enable/Disable│  │              │    │                         │
│  └───────────────┘  │              │    │                         │
│                     │   0x055C     │    ▼                         │
│  ┌───────────────┐  │◄──TELEM─────│  PWM status telemetry        │
│  │ PWM Status    │  │              │  (freq, duty, running)       │
│  │ Actual Freq   │  │              │                              │
│  │ Actual Duty   │  │              │                              │
│  └───────────────┘  │              │                              │
│                     │   0x055D     │                              │
│  ┌───────────────┐  │◄──TELEM─────│  Input Capture telemetry     │
│  │ Input Capture │  │              │  (meas. freq, duty, edges)   │
│  │ Measured Freq │  │              │                              │
│  │ Measured Duty │  │              │  input_capture.c             │
│  │ Signal Status │  │              │    (TIM3 CH1+CH2, PA6)       │
│  └───────────────┘  │              │                              │
└─────────────────────┘              └──────────────────────────────┘
```

---

## Phase 1 — PWM CAN Protocol & Firmware Command

> **Note**: This phase has already been completed in the current codebase!

### 1.1 Protocol Definition

Added to `protocol/can_protocol.yaml`:

```yaml
# Under commands:
pwm_set:
  code: 0x40
  description: "Set PWM output frequency and duty cycle"
  # Byte 0: command (0x40)
  # Bytes 1-4: frequency_hz, uint32 LE (1..1000000, 0 = stop)
  # Byte 5: duty_percent (0..100)
  # Bytes 6-7: reserved, zero

# Under identifiers:
pwm_status:
  id: 0x055C
  type: standard
  direction: mcu_to_gui
  description: "PWM output status"
  # Byte 0: running (0/1)
  # Byte 1: duty_percent (0..100)
  # Bytes 2-5: actual_frequency_hz, uint32 LE
  # Bytes 6-7: reserved
```

### 1.2 Deliverables (COMPLETED)

- [x] PWM start/stop/update via CAN command `0x40`
- [x] PWM status telemetry on `0x055C` (actual freq, duty, running state)
- [x] Validation: frequency 1–1 MHz, duty 0–100%, frequency 0 = stop
- [x] Protocol regeneration from YAML

---

## Phase 2 — GUI PWM Control Panel

> **Goal**: PySide6 panel with sliders/spinboxes for live PWM adjustment.

### 2.1 Python GUI Changes

| File | Change |
|---|---|
| `python/can_gui_app/pwm_panel.py` | **New** — PWM control and status panel |
| `python/can_gui_app/can_app_controller.py` | Add `send_pwm_command()` and `0x055C` telemetry handler |
| `python/can_gui_app/main_window_view.py` | Add PWM panel to navigation/pages |

### 2.2 PWM Panel Design

```text
┌─ PWM Output Control ──────────────────────────────────┐
│                                                        │
│  Frequency   [====|==========] ◄──► [  10,000 ] Hz    │
│              1 Hz              1 MHz                   │
│                                                        │
│  Duty Cycle  [========|======] ◄──► [    90   ] %     │
│              0%               100%                     │
│                                                        │
│  [ ● Enable PWM ]    [ ○ Disable PWM ]                │
│                                                        │
├─ PWM Status ──────────────────────────────────────────┤
│  State:            ● Running                           │
│  Actual Frequency: 10,000 Hz                           │
│  Actual Duty:      90%                                 │
│  Output Pin:       PA0 (TIM2 CH1)                      │
└────────────────────────────────────────────────────────┘
```

**UI details:**
- Logarithmic frequency slider (1 Hz → 1 MHz) with editable spinbox
- Linear duty cycle slider (0–100%) with editable spinbox
- Enable/Disable toggle buttons
- Live status readback from `0x055C` telemetry
- Frequency presets: 1 kHz, 10 kHz, 100 kHz, 1 MHz

### 2.3 Deliverables

- [x] `pwm_panel.py` with frequency slider, duty slider, enable/disable
- [x] Controller integration for sending `0x40` and receiving `0x055C`
- [x] Panel added to main window navigation
- [x] Python GUI tests for the new panel

---

## Phase 3 — Input Capture Firmware

> **Goal**: Measure frequency and duty cycle of an external signal using TIM3.

### 3.1 Hardware Design

| Item | Configuration |
|---|---|
| Timer | TIM3 (16-bit general-purpose) |
| Mode | PWM Input Mode (dual-channel capture on one pin) |
| Input pin | PA6 (TIM3_CH1) |
| CH1 | Direct input, rising edge → captures **period** |
| CH2 | Indirect input, falling edge → captures **pulse width** |
| Slave mode | Reset on CH1 rising edge (auto-clears counter) |
| Clock | 1 MHz (prescaler = 63 from 64 MHz APB1) |
| Measurable range | ~16 Hz to 500 kHz (16-bit counter at 1 MHz) |

### 3.2 Firmware Module

**New files**: `Core/Inc/input_capture.h`, `Core/Src/input_capture.c`

```c
typedef struct {
    uint8_t  signal_detected;   // 1 if valid edges seen recently
    uint32_t frequency_hz;      // measured frequency
    uint8_t  duty_percent;      // measured duty cycle
    uint32_t period_ticks;      // raw period capture (CCR1)
    uint32_t pulse_ticks;       // raw pulse capture (CCR2)
    uint32_t edge_count;        // estimated rising-edge count
    uint32_t overflow_count;    // signal-loss transitions
    uint32_t counter_clock_hz;  // capture timer clock
} Input_Capture_State_t;

Input_Capture_Result_t Input_Capture_Init(TIM_HandleTypeDef *htim, uint32_t counter_clock_hz);
Input_Capture_State_t  Input_Capture_GetState(void);
void Input_Capture_Process(void);  // called from the application main loop
```

**Key implementation notes:**
- Main-loop service samples hardware-updated `CCR1` (period) and `CCR2` (pulse)
- Frequency = `counter_clock_hz / CCR1`
- Duty = `(CCR2 * 100) / CCR1`
- A 150 ms last-capture timeout detects signal loss without per-edge interrupts
- `Input_Capture_GetState()` returns a snapshot for main-loop telemetry

### 3.3 CubeMX Changes

- Enable TIM3 in CubeMX (`can_gui.ioc`)
- Configure TIM3 CH1 as Input Capture Direct, CH2 as Input Capture Indirect
- Set prescaler to 63 (1 MHz counter clock)
- Leave TIM3 global interrupt disabled; capture registers are polled
- Map PA6 to TIM3_CH1

### 3.4 Protocol Extension

Add to `protocol/can_protocol.yaml`:

```yaml
# Under identifiers:
input_capture_status:
  id: 0x055D
  type: standard
  direction: mcu_to_gui
  description: "Input capture measurement"
  # Byte 0: signal_detected (0/1)
  # Byte 1: duty_percent (0..100)
  # Bytes 2-5: frequency_hz, uint32 LE
  # Bytes 6-7: estimated edge_count low 16 bits, uint16 LE
```

### 3.5 Deliverables

- [x] `input_capture.c/h` driver using TIM3 PWM Input Mode
- [x] CubeMX TIM3 configuration on PA6
- [x] Polled period/pulse capture with signal-loss detection
- [x] Telemetry on `0x055D` published periodically
- [x] Protocol YAML updated and regenerated

---

## Phase 4 — GUI Input Capture Display

> **Goal**: Display live input capture measurements in the GUI.

### 4.1 Panel Design

```text
┌─ Input Capture ───────────────────────────────────────┐
│                                                        │
│  Signal Status:    ● Detected                          │
│  Frequency:        10,000 Hz                           │
│  Duty Cycle:       50.0%                               │
│  Edge Count:       1,247,832                           │
│  Input Pin:        PA6 (TIM3 CH1)                      │
│                                                        │
│  Measurable Range: 16 Hz – 500 kHz                     │
│                                                        │
│  ┌─ Signal Indicator ─────────────────────────────┐   │
│  │  ████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░  │   │
│  │  ◄──── 50% ────►                               │   │
│  └────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────┘
```

### 4.2 Python GUI Changes

| File | Change |
|---|---|
| `python/can_gui_app/pwm_panel.py` | Add Input Capture section below PWM control |
| `python/can_gui_app/can_app_controller.py` | Add `0x055D` telemetry handler |
| `python/can_gui_app/protocol.py` | Add input capture constants |

### 4.3 Deliverables

- [x] Input capture display integrated into PWM panel
- [x] Live frequency, duty, and signal-status readback
- [x] Visual duty-cycle bar indicator
- [x] Python GUI tests

---

## Phase 5 — Loopback Built-In Test & Polish

> **Goal**: Wire PA0 (PWM out) to PA6 (input capture in) for a closed-loop
> self-test where the MCU controls and evaluates PWM while the GUI initiates
> the test and displays progress.

### 5.1 Features

- Live loopback verification: output matches measurement within tolerance
- Non-blocking MCU state machine independent of GUI/CAN timing
- Frequency points at 1 kHz, 10 kHz, 100 kHz, and 500 kHz
- Duty points at 10%, 25%, 50%, 75%, and 90% at 10 kHz
- Signal-loss check after stopping PWM
- 2% frequency and 2 percentage-point duty tolerances
- Previous PWM state restoration after pass, failure, or cancellation
- CAN start/cancel command plus status and per-point result telemetry
- Error/mismatch highlighting in the GUI
- Documentation update (`README.md`, connection diagram)

### 5.2 Deliverables

- [x] Live loopback comparison in GUI
- [x] MCU-managed 10-point built-in test with pass/fail report
- [x] Start/cancel controls and live progress display
- [x] CAN command `0x41`, status `0x055E`, and result `0x055F`
- [x] PWM state restoration after terminal outcomes
- [x] README updated with PWM and input capture sections
- [x] Host tests for firmware state machine and protocol encoding
- [x] Tests for GUI protocol/controller and panel rendering
- [x] Terminal built-in-test results retained across MCU resets and synced to CSV

---

## Hardware Wiring Summary

```text
NUCLEO-H7A3ZI-Q

  PA0 ──── TIM2 CH1 ──── PWM Output (oscilloscope / loopback)
     │
     │  (jumper wire for loopback test)
     ▼
  PA6 ──── TIM3 CH1 ──── Input Capture (frequency/duty measurement)
```

> [!TIP]
> For loopback testing, simply connect PA0 to PA6 with a jumper wire on the
> Nucleo board. The PWM output will be measured by the input capture and both
> values will appear in the GUI.
