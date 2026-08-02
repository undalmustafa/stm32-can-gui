# Firmware API Call Context Contract

Project-owned module headers declare a machine-checked call-context block:

```c
/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: Module_NotifyFromIsr
 * CALL_CONTEXT_INTERNAL: none
 */
```

The categories have the following meaning:

- `MAIN_LOOP_ONLY`: callable during startup or from the cooperative main loop.
  It may mutate shared module state, call HAL services, enqueue transport data,
  log events or execute a state machine. It must never be called from an ISR.
- `ISR_SAFE`: bounded and non-blocking. It may only copy fixed-size state, set a
  flag/counter or perform deterministic computation. It must not wait, log,
  allocate, execute transport/state-machine work or call a blocking HAL API.
- `INTERNAL`: owned by the named subsystem and unavailable as a general
  application API. Internal APIs remain main-loop-only unless explicitly listed
  under `CALL_CONTEXT_ISR_SAFE`.

`CALL_CONTEXT_DEFAULT` applies to every function declared by the header unless
the function is named in an exception list. `all` and `none` are the only
non-symbol values permitted. An ISR-facing API must use a `FromIsr` or
`RecordIsr` suffix and appear in the header's ISR-safe list. APIs in a header
whose default is already `ISR_SAFE` do not require the suffix.

The contract checker covers every project module header below `Core/Inc` while
excluding Cube-generated configuration, vector and generated protocol headers.
Run it locally with:

```sh
make -C tests test-call-context
```

This is an architectural boundary check, not a substitute for reviewing the
implementation of each ISR-safe function or running the target interrupt-storm
acceptance test in `docs/INTERRUPT_POLICY.md`.
