# Stack Usage Budget

Release firmware stack use is bounded by combining GCC `.su` files produced
with `-fstack-usage` and the call graph extracted from
`arm-none-eabi-objdump` disassembly.

```sh
make CONFIG=release
make CONFIG=release stack-report
```

The second target rebuilds the firmware only when Make dependencies require
it. It normally produces `build/release/stack-usage.txt` and
`build/release/stack-usage.json`. CI retains both files as artifacts and fails
when the budget is exceeded.

## Budget model

- The actual reservation is read from the linker `_Min_Stack_Size` symbol.
- The deepest direct main-loop call path is calculated.
- Active IRQ priorities from `app_irq_policy.h` are added under the
  conservative assumption that every higher-priority interrupt nests over
  every lower-priority interrupt.
- Each exception entry reserves 108 bytes for the Cortex-M7 extended
  floating-point frame and alignment.
- A 256-byte reserve covers an external runtime/library chain without `.su`
  data; each unresolved indirect call receives another 256-byte reserve.
- At least 256 bytes of safety margin must remain after the calculated
  envelope.
- Dynamic or recursive stack use is rejected fail-closed.

The current conservative release envelope is 1,288 bytes. The former 1 KiB
reservation could not cover extended exception nesting and the mandatory
margin. The minimum accepted by this model is 1,544 bytes, so the linker and
CubeMX contract reserve 2 KiB. This leaves 760 bytes of raw margin and 504
bytes after the policy margin.

## Limits

This report is static and conservative; it does not replace runtime watermark
measurement. GCC records marked `ignoring_inline_asm` and external symbols
covered by reserves are listed explicitly. When adding an IRQ, define its root
in `scripts/stack_budget.json` and its priority macro in
`app_irq_policy.h`. Fault/NMI acceptance tests and on-target stack watermark
measurements remain part of HIL validation.
