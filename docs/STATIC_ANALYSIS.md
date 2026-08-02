# Static Analysis Contract

Firmware application sources are analyzed with Cppcheck at two levels:

1. The report pass writes `warning`, `style`, `performance`, and `portability`
   findings to `build/static-analysis/cppcheck.txt`.
2. The quality gate writes `error`, `warning`, `performance`, and
   `portability` findings to `build/static-analysis/cppcheck.xml` and fails on
   any finding. Style suggestions remain visible but do not stop CI.

The analysis uses the STM32H7 32-bit ARM ABI and the firmware build defines.
`Core/Src` is project scope. Third-party HAL/CMSIS code under `Drivers/` is
excluded from the report and gate. Firmware compilation still builds those
dependencies with `-Wall -Wextra`, while project-owned `Core` sources retain
the strict `-Wconversion -Wshadow -Wundef -Werror` policy.

Run locally with:

```sh
make static-analysis
```

To select another Cppcheck binary or output directory:

```sh
make static-analysis CPPCHECK=/path/to/cppcheck \
  STATIC_ANALYSIS_DIR=/tmp/can-gui-analysis
```

CI retains both text and XML reports as artifacts whether the job succeeds or
fails. Suppressions are allowed only for verified tool false positives or
external ABI contracts. Every suppression must be narrow and justified next
to the affected code.
