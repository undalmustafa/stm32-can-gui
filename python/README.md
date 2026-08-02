# STM32 CAN GUI

A PySide6 desktop application for configuring and monitoring the STM32H7
controller over SocketCAN or PCAN, running UDS diagnostics and signed firmware
updates, and exporting host and retained MCU event logs.

The original monolithic application was split into focused services,
controllers, and panels. `can_gui.py` is now only the composition root that
wires those components together.

## Installation

From this directory on Windows Command Prompt or PowerShell:

```powershell
py -m venv .venv
.venv\Scripts\activate
py -m pip install -r requirements.txt
py can_gui.py
```

`requirements.txt` is a complete direct and transitive dependency lock for
Python 3.13. For a controlled upgrade, change direct requirements in
`requirements.in`, update the resolved versions in `requirements.txt`, and
run from the repository root:

```powershell
make -C tests test-python-dependencies
```

`run_gui.bat` may be used after the first installation. Windows requires the
PEAK driver and PCAN-Basic runtime for a PCAN adapter. Defaults are
`pcan/PCAN_USBBUS1` on Windows, `socketcan/can0` on Linux, and 500,000 bit/s.

Before opening the GUI on Linux, configure SocketCAN:

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 restart-ms 100
sudo ip link set can0 up
```

Do not copy `can_gui.py` by itself; keep the `can_gui_app` package beside it.

## Layout

```text
python/
├── can_gui.py
├── run_gui.bat
├── requirements.in
├── requirements.txt
├── can_gui_app/
└── tests/
```

## Module responsibilities

| Module | Responsibility |
|---|---|
| `can_gui.py` | Application composition root and top-level error handling |
| `protocol.py` | Generated CAN IDs, commands, status, and payload constants |
| `can_session.py` | SocketCAN/PCAN connection, TX/RX, ISO-TP, and transport metrics |
| `isotp_client.py` | Non-blocking Classic CAN ISO-TP request/response transport |
| `uds_client.py` | Ordered UDS requests and response validation |
| `diagnostics_controller.py` | F100-F103 DID polling, big-endian decode, and error deduplication |
| `diagnostics_panel.py` | Live protocol, startup, runtime, and reset evidence |
| `flash_controller.py` | Sequential fail-closed inactive-slot firmware update workflow |
| `flash_panel.py` | Signed artifact selection and firmware update progress |
| `can_health.py` | CAN health state machine and BUS_HEAVY/BUS_OFF recovery reporting |
| `timing_controller.py` | DWT service/ACK telemetry and bounded history |
| `timing_panel.py` | Service timing table, overrun state, and sparklines |
| `can_connection_panel.py` | Interface, channel, bitrate, and CAN health controls |
| `can_app_controller.py` | Slot/LED commands and application status decode |
| `can_app_panel.py` | Slot/LED controls and effective-state display |
| `slot_widget.py` | Reusable transmit-slot configuration widget |
| `rtc_controller.py` | RTC/alarm protocol, diagnostics, and state management |
| `rtc_panel.py` | RTC calendar/alarm controls and live values |
| `tic12400_controller.py` | Confirmed switch state and polarity orchestration |
| `tic12400_panel.py` | End-user switch state and supported polarity controls |
| `pwm_panel.py` | PWM, capture, and built-in-test controls and results |
| `csv_event_logger.py` | Daily host event CSV storage |
| `stm32_log_sync.py` | Validation and transfer of retained MCU event records |
| `event_log_panel.py` | Host/MCU log controls and synchronization status |
| `main_window_view.py` | Main window and task-oriented page layout |
| `application_timers.py` | RX, health, log synchronization, and diagnostic timers |

## Periodic work

| Operation | Period |
|---|---:|
| CAN RX polling | 50 ms |
| MCU log synchronization | 50 ms |
| CAN health evaluation | 250 ms |
| UDS diagnostic polling | 1,000 ms |

## Log files

- `events_YYYYMMDD.csv`: GUI, CAN health, RTC, flash, and application events.
- `stm32_events_YYYYMMDD_HHMMSS_ffffff.csv`: retained MCU records transferred
  after CRC and commit-marker validation.

The default location is `python/logs`; it can be changed from the Logs page.

## Hardware-free tests

From the repository root, use the locked virtual environment:

```bash
QT_QPA_PLATFORM=offscreen make -C tests \
  PYTHON="$PWD/python/.venv/bin/python" test-python
```

The suite covers application composition, CAN/ISO-TP/UDS transport, signed
artifact validation, sequential firmware update and retry behavior, health
state machines, RTC, TIC12400 and PWM presentation, timing graphs, both CSV
layers, and panel interactions. These tests do not prove electrical behavior,
real flash programming, CAN timing, or reset-to-boot behavior.

## Hardware acceptance checklist

1. Verify `WAIT_RX -> ACTIVE` after connecting through PCAN or SocketCAN.
2. Verify slot 1/2 counter traffic and configured cycle times.
3. Exercise LED1/LED2 commands and physical override behavior.
4. Exercise RTC read/write, weekday calculation, and alarms.
5. Verify creation of GUI and MCU CSV logs.
6. Inject CAN faults and observe BUS_HEAVY/BUS_OFF and recovery.
7. Verify F100-F103 values populate within two diagnostic poll cycles and
   time out visibly when the ECU is disconnected.
8. Use a signed slot artifact to validate erase, transfer, acceptance, reset,
   boot confirmation, and rollback on the target.
