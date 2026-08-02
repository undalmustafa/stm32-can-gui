#!/usr/bin/env bash

set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
readonly CPPCHECK_BIN="${CPPCHECK:-cppcheck}"
readonly REPORT_DIR="${STATIC_ANALYSIS_DIR:-build/static-analysis}"
readonly TEXT_REPORT="${REPORT_DIR}/cppcheck.txt"
readonly XML_REPORT="${REPORT_DIR}/cppcheck.xml"

if ! command -v "${CPPCHECK_BIN}" >/dev/null 2>&1; then
    echo "cppcheck executable not found: ${CPPCHECK_BIN}" >&2
    exit 127
fi

cd "${REPO_ROOT}"
mkdir -p "${REPORT_DIR}"

# Analyze project-owned firmware sources with the target ABI. Driver findings are
# excluded because the repository consumes STM32 HAL/CMSIS as third-party code.
readonly -a COMMON_ARGS=(
    --quiet
    --std=c11
    --platform=arm32-wchar_t4
    --inline-suppr
    --suppress=missingIncludeSystem
    '--suppress=*:*Drivers/*'
    -D__GNUC__=1
    -DUSE_PWR_DIRECT_SMPS_SUPPLY
    -DUSE_HAL_DRIVER
    -DSTM32H7A3xxQ
    -ICore/Inc
    -IDrivers/STM32H7xx_HAL_Driver/Inc
    -IDrivers/STM32H7xx_HAL_Driver/Inc/Legacy
    -IDrivers/BSP/STM32H7xx_Nucleo
    -IDrivers/CMSIS/Device/ST/STM32H7xx/Include
    -IDrivers/CMSIS/Include
    Core/Src
)

echo "Running Cppcheck report scan..."
"${CPPCHECK_BIN}" \
    --enable=warning,style,performance,portability \
    --output-file="${TEXT_REPORT}" \
    "${COMMON_ARGS[@]}"

if [[ -s "${TEXT_REPORT}" ]]; then
    cat "${TEXT_REPORT}"
else
    echo "No report-level findings."
fi

echo "Running Cppcheck quality gate..."
set +e
"${CPPCHECK_BIN}" \
    --enable=warning,performance,portability \
    --error-exitcode=1 \
    --xml \
    --xml-version=2 \
    --output-file="${XML_REPORT}" \
    "${COMMON_ARGS[@]}"
readonly GATE_STATUS=$?
set -e

if ((GATE_STATUS != 0)); then
    echo "Cppcheck quality gate failed; inspect ${TEXT_REPORT} and ${XML_REPORT}." >&2
    exit "${GATE_STATUS}"
fi

echo "Cppcheck quality gate passed. Reports: ${TEXT_REPORT}, ${XML_REPORT}"
