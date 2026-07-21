"""Build and run the watchdog unit test with an available host compiler."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = ROOT / "build" / "host-tests"
FAKE_HAL = ROOT / "tests" / "host" / "watchdog_fake_hal.h"
TEST_SOURCE = ROOT / "tests" / "host" / "test_app_watchdog.c"
WATCHDOG_SOURCE = ROOT / "Core" / "Src" / "app_watchdog.c"
EVIDENCE_SOURCE = ROOT / "Core" / "Src" / "app_watchdog_evidence.c"
WATCHDOG_INCLUDE = ROOT / "Core" / "Inc"
HAL_INCLUDE = ROOT / "Drivers" / "STM32H7xx_HAL_Driver" / "Inc"


def msvc_environment() -> dict[str, str]:
    installer = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)"))
    vswhere = installer / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.is_file():
        raise RuntimeError("Visual Studio environment helper was not found")

    result = subprocess.run(
        [str(vswhere), "-latest", "-property", "installationPath"],
        check=True,
        capture_output=True,
        text=True,
    )
    vcvars = Path(result.stdout.strip()) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    if not vcvars.is_file():
        raise RuntimeError("Visual Studio x64 build environment was not found")

    result = subprocess.run(
        f'call "{vcvars}" >nul && set',
        shell=True,
        check=True,
        capture_output=True,
        text=True,
    )
    environment = os.environ.copy()
    for line in result.stdout.splitlines():
        if "=" in line:
            name, value = line.split("=", 1)
            environment[name] = value
    return environment


def compile_test() -> Path:
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    environment = None

    if os.name == "nt" and shutil.which("cl"):
        environment = msvc_environment()
        executable = BUILD_DIR / "test_app_watchdog.exe"
        command = [
            "cl",
            "/nologo",
            "/std:c11",
            "/W4",
            "/WX",
            "/DAPP_WATCHDOG_TEST_HOOKS",
            "/DAPP_WATCHDOG_HOST_TEST",
            f"/FI{FAKE_HAL}",
            f"/I{WATCHDOG_INCLUDE}",
            f"/I{HAL_INCLUDE}",
            str(WATCHDOG_SOURCE),
            str(EVIDENCE_SOURCE),
            str(TEST_SOURCE),
            f"/Fe:{executable}",
        ]
    else:
        compiler = next(
            (candidate for candidate in ("cc", "gcc", "clang")
             if shutil.which(candidate)),
            None,
        )
        if compiler is None:
            raise RuntimeError("No supported host C compiler was found")

        executable = BUILD_DIR / "test_app_watchdog"
        command = [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DAPP_WATCHDOG_TEST_HOOKS",
            "-DAPP_WATCHDOG_HOST_TEST",
            "-include",
            str(FAKE_HAL),
            "-I",
            str(WATCHDOG_INCLUDE),
            "-I",
            str(HAL_INCLUDE),
            str(WATCHDOG_SOURCE),
            str(EVIDENCE_SOURCE),
            str(TEST_SOURCE),
            "-o",
            str(executable),
        ]

    subprocess.run(command, cwd=BUILD_DIR, env=environment, check=True)
    return executable


def main() -> int:
    executable = compile_test()
    subprocess.run([str(executable)], cwd=ROOT, check=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"watchdog host test failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
