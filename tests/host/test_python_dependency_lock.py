#!/usr/bin/env python3
"""Validate exact Python dependency locks without importing third parties."""

import re
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
EXACT_REQUIREMENT = re.compile(
    r"^(?P<name>[A-Za-z0-9][A-Za-z0-9._-]*)=="
    r"(?P<version>[A-Za-z0-9][A-Za-z0-9.!+_-]*)$"
)


def normalized_name(name: str) -> str:
    return re.sub(r"[-_.]+", "-", name).lower()


def read_lock(relative_path: str) -> dict[str, str]:
    path = REPOSITORY_ROOT / relative_path
    requirements: dict[str, str] = {}

    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(),
        start=1,
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        match = EXACT_REQUIREMENT.fullmatch(line)
        if match is None:
            raise AssertionError(
                f"{relative_path}:{line_number}: dependency is not exact: "
                f"{line}"
            )

        name = normalized_name(match.group("name"))
        if name in requirements:
            raise AssertionError(
                f"{relative_path}:{line_number}: duplicate dependency: {name}"
            )
        requirements[name] = match.group("version")

    if not requirements:
        raise AssertionError(f"{relative_path}: dependency list is empty")
    return requirements


def main() -> None:
    direct = read_lock("python/requirements.in")
    runtime = read_lock("python/requirements.txt")
    protocol = read_lock("protocol/requirements.txt")

    expected_runtime = {
        "packaging",
        "pyside6",
        "pyside6-addons",
        "pyside6-essentials",
        "python-can",
        "shiboken6",
        "typing-extensions",
        "wrapt",
    }
    if set(runtime) != expected_runtime:
        raise AssertionError(
            "python/requirements.txt must contain the reviewed runtime closure"
        )

    if set(direct) != {"pyside6", "python-can"}:
        raise AssertionError(
            "python/requirements.in must contain only direct GUI dependencies"
        )
    for name, version in direct.items():
        if runtime.get(name) != version:
            raise AssertionError(
                f"direct dependency {name} is not locked to {version}"
            )

    pyside_version = runtime["pyside6"]
    for name in ("pyside6-addons", "pyside6-essentials", "shiboken6"):
        if runtime[name] != pyside_version:
            raise AssertionError(
                f"{name} must match PySide6 version {pyside_version}"
            )

    if set(protocol) != {"pyyaml"}:
        raise AssertionError(
            "protocol/requirements.txt must lock only the generator dependency"
        )

    print("PASS: Python direct, runtime, and protocol dependencies are locked")


if __name__ == "__main__":
    main()
