# Interrupt Priority and ISR Contract

The firmware uses `NVIC_PRIORITYGROUP_4`: all four implemented Cortex-M7
priority bits select preemption priority and every application interrupt uses
subpriority zero. Lower numeric values have higher urgency.

## Priority allocation

| Priority | Owner | Current IRQ | Purpose |
|---:|---|---|---|
| 0 | Safety timer | Reserved | Future hardware safe-state deadline or break supervision |
| 1 | HAL timeout source | `SysTick_IRQn` | Keeps `HAL_GetTick()` and bounded main-loop transfers progressing |
| 2 | CAN | `FDCAN1_IT0_IRQn` | RX/error/TX-complete event capture |
| 3 | Switch monitor | `EXTI9_5_IRQn` | TIC12400 interrupt edge capture |
| 4–14 | Application timers | Reserved | Future capture/control timers, allocated by deadline |
| 15 | Human interface | `EXTI15_10_IRQn` | Nucleo user-button request flag |

`Core/Inc/app_irq_policy.h` is the firmware source of truth. Preprocessor
checks reject an invalid built-in ordering, `main.c` checks the HAL/BSP
configuration against it, and `can_gui.ioc` carries the same values so a
CubeMX regeneration preserves the policy.

SysTick deliberately preempts every current application peripheral ISR. This
prevents a future interrupt path from silently stopping the HAL millisecond
timeout source. It is a second line of defence, not permission to perform a
blocking operation in an ISR.

## ISR execution contract

Interrupt context may only perform bounded work:

- acknowledge or dispatch the peripheral interrupt;
- read a bounded set of peripheral status registers;
- set a `volatile` flag or increment a monotonic 32-bit event counter;
- copy fixed-size diagnostic values that are consumed in the main loop.

Interrupt context must not:

- call blocking SPI, I2C, UART, or delay APIs;
- poll on `HAL_GetTick()` or wait for another interrupt;
- parse CAN commands or execute application state machines;
- write the event log, enqueue transport frames, or format diagnostics;
- acquire a lock also used by a lower-priority context.

Current paths follow that split:

| IRQ callback | ISR work | Deferred main-loop work |
|---|---|---|
| FDCAN RX FIFO0 | Map flags, sample fill level, increment counters | Drain frames, validate, execute command, enqueue ACK |
| FDCAN error/TX complete | Snapshot PSR/ECR or increment completion counter | Bus-off recovery, event logging, health telemetry |
| TIC12400 EXTI | Increment edge counter and set pending flag | SPI status read, input sampling, recovery |
| User button EXTI | Set control-access request flag | Open/expire access window and log the transition |

Project API declarations carry the same boundary through the standardized
header contracts documented in `docs/CALL_CONTEXT.md`. Any new ISR-facing API
must be explicitly listed as `ISR_SAFE` and use a `FromIsr` or `RecordIsr`
suffix unless its entire module is ISR-safe by default.

## Adding an interrupt

Before enabling a new IRQ:

1. Assign its deadline class in `app_irq_policy.h`; do not select an unused
   number ad hoc in generated code.
2. Keep its handler bounded according to the contract above and expose an
   explicitly named `...FromIsr()` seam when it touches application state.
3. Add the same priority to `can_gui.ioc` and use the policy macro in the MSP
   or GPIO initialization code.
4. Extend `App_IrqPolicy_Config_t` and its host tests if the new interrupt can
   affect the timeout or service ordering.
5. On target, inspect the NVIC priority registers and run the relevant
   interrupt-storm acceptance test before declaring the change complete.

## Target acceptance test

1. Confirm grouping 4 and priorities `SysTick=1`, `FDCAN1_IT0=2`,
   `EXTI9_5=3`, and `EXTI15_10=15` in the debugger after initialization.
2. Apply a sustained CAN RX burst while generating TIC12400 edges and verify
   SysTick continues advancing without gaps larger than 1 ms.
3. Repeat with the user button interrupt active; CAN RX full/lost counters
   must not increase below the separately defined bus-capacity limit.
4. Inject SPI MISO and I2C SDA stalls from main-loop context and verify their
   10 ms HAL timeouts still expire while CAN and EXTI interrupts are active.
5. Record firmware commit, traffic rate, interrupt rates, maximum main-loop
   time, and RX lost/full counters with the result.
