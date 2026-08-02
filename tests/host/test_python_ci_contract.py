#!/usr/bin/env python3
"""Enforce the single-workflow Python GUI CI contract."""

import re
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CI_WORKFLOW = REPOSITORY_ROOT / ".github" / "workflows" / "ci.yml"
LEGACY_WORKFLOW = (
    REPOSITORY_ROOT / ".github" / "workflows" / "python-tests.yml"
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    require(not LEGACY_WORKFLOW.exists(),
            "legacy python-tests.yml must remain removed")

    workflow = CI_WORKFLOW.read_text(encoding="utf-8")
    require(len(re.findall(r"(?m)^  gui:$", workflow)) == 1,
            "CI must define exactly one gui job")
    match = re.search(
        r"(?ms)^  gui:\n(?P<body>.*?)(?=^  [A-Za-z0-9_-]+:\n|\Z)",
        workflow,
    )
    require(match is not None, "CI gui job body must be readable")
    gui_job = match.group("body")

    require(gui_job.count("uses: actions/setup-python@v6") == 1,
            "GUI job must set up Python exactly once")
    require(gui_job.count('python-version: "3.13"') == 1,
            "GUI job must use the project Python 3.13 version")
    require(
        gui_job.count(
            "python -m pip install --requirement python/requirements.txt"
        ) == 1,
        "GUI dependencies must be installed exactly once",
    )
    python_test_steps = re.findall(
        r"(?m)^\s+run: make -C tests test-python$",
        gui_job,
    )
    require(len(python_test_steps) == 1,
            "GUI checks must use the Make test-python target exactly once")
    require(gui_job.count("QT_QPA_PLATFORM: offscreen") == 1,
            "GUI tests must use the headless Qt platform")

    print("PASS: Python GUI CI uses one workflow, version, and install")


if __name__ == "__main__":
    main()
