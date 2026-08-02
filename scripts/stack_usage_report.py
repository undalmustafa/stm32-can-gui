#!/usr/bin/env python3
"""Estimate Cortex-M stack usage from GCC .su files and an objdump listing."""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import json
from pathlib import Path
import re
import sys
from typing import Any


FUNCTION_HEADER_RE = re.compile(r"^\s*[0-9a-fA-F]+\s+<([^>]+)>:$")
DIRECT_CALL_RE = re.compile(
    r"\bblx?(?:\.w)?\s+(?:0x)?[0-9a-fA-F]+\s+<([^>]+)>"
)
INDIRECT_CALL_RE = re.compile(
    r"\bblx(?:\.w)?\s+(?:r(?:1[0-5]|[0-9])|ip|lr)\b"
)
TAIL_CALL_RE = re.compile(
    r"\bb(?:\.w|\.n)?\s+(?:0x)?[0-9a-fA-F]+\s+<([^>]+)>"
)
OPTIMIZED_SUFFIX_RE = re.compile(r"\.(constprop|isra|part)\.\d+$")
LINKER_STACK_RE = re.compile(
    r"^\s*_Min_Stack_Size\s*=\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*;",
    re.MULTILINE,
)
INTEGER_DEFINE_RE = re.compile(
    r"^\s*#define\s+([A-Za-z_]\w*)\s+"
    r"(0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*\b",
    re.MULTILINE,
)


class StackAnalysisError(RuntimeError):
    """Raised when stack inputs or the reachable call graph are invalid."""


@dataclass(frozen=True)
class StackEntry:
    name: str
    bytes_used: int
    qualifiers: tuple[str, ...]
    source: str


@dataclass
class CallNode:
    calls: set[str] = field(default_factory=set)
    tail_calls: set[str] = field(default_factory=set)
    has_indirect_call: bool = False


@dataclass(frozen=True)
class PathEstimate:
    bytes_used: int
    path: tuple[str, ...]
    assumed_frames: tuple[str, ...] = ()
    indirect_callers: tuple[str, ...] = ()


def normalize_symbol(name: str) -> str:
    """Match numbered objdump optimization suffixes to GCC .su symbols."""
    return OPTIMIZED_SUFFIX_RE.sub(r".\1", name)


def parse_stack_usage(directory: Path) -> dict[str, StackEntry]:
    entries: dict[str, StackEntry] = {}
    files = sorted(directory.rglob("*.su"))
    if not files:
        raise StackAnalysisError(f"no .su files found below {directory}")

    for path in files:
        for line_number, raw_line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), start=1
        ):
            if not raw_line.strip():
                continue
            try:
                location, byte_text, qualifier_text = raw_line.split("\t", 2)
                source_location, function = location.rsplit(":", 1)
                source, _source_line, _column = source_location.rsplit(":", 2)
                bytes_used = int(byte_text)
            except ValueError as error:
                raise StackAnalysisError(
                    f"invalid .su record at {path}:{line_number}: {raw_line}"
                ) from error

            name = normalize_symbol(function)
            qualifiers = tuple(
                item.strip() for item in qualifier_text.split(",") if item.strip()
            )
            entry = StackEntry(name, bytes_used, qualifiers, source)
            previous = entries.get(name)
            if previous is None or entry.bytes_used > previous.bytes_used:
                entries[name] = entry

    return entries


def parse_disassembly(path: Path) -> dict[str, CallNode]:
    if not path.is_file():
        raise StackAnalysisError(f"disassembly listing not found: {path}")

    graph: dict[str, CallNode] = {}
    current: str | None = None
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        header = FUNCTION_HEADER_RE.match(line)
        if header:
            current = normalize_symbol(header.group(1))
            graph.setdefault(current, CallNode())
            continue
        if current is None:
            continue

        node = graph[current]
        direct_call = DIRECT_CALL_RE.search(line)
        if direct_call:
            target = direct_call.group(1)
            if "+0x" not in target:
                node.calls.add(normalize_symbol(target))
            continue
        if INDIRECT_CALL_RE.search(line):
            node.has_indirect_call = True
            continue
        tail_call = TAIL_CALL_RE.search(line)
        if tail_call:
            target = tail_call.group(1)
            if "+0x" not in target:
                target = normalize_symbol(target)
                if target != current:
                    node.tail_calls.add(target)

    if not graph:
        raise StackAnalysisError(f"no function disassembly found in {path}")
    return graph


def parse_linker_stack_budget(path: Path) -> int:
    if not path.is_file():
        raise StackAnalysisError(f"linker script not found: {path}")
    match = LINKER_STACK_RE.search(path.read_text(encoding="utf-8"))
    if match is None:
        raise StackAnalysisError(f"_Min_Stack_Size is absent from {path}")
    return int(match.group(1), 0)


def parse_integer_macros(path: Path) -> dict[str, int]:
    if not path.is_file():
        raise StackAnalysisError(f"IRQ policy header not found: {path}")
    text = path.read_text(encoding="utf-8")
    return {
        name: int(value, 0) for name, value in INTEGER_DEFINE_RE.findall(text)
    }


class StackEstimator:
    def __init__(
        self,
        entries: dict[str, StackEntry],
        graph: dict[str, CallNode],
        external_chain_reserve: int,
        indirect_reserve: int,
    ) -> None:
        self.entries = entries
        self.graph = graph
        self.external_chain_reserve = external_chain_reserve
        self.indirect_reserve = indirect_reserve
        self._memo: dict[str, PathEstimate] = {}

    def estimate(self, root: str) -> PathEstimate:
        root = normalize_symbol(root)
        if root not in self.entries and root not in self.graph:
            raise StackAnalysisError(f"configured root is absent: {root}")
        return self._estimate(root, ())

    def _estimate(self, function: str, active: tuple[str, ...]) -> PathEstimate:
        cached = self._memo.get(function)
        if cached is not None:
            return cached
        if function in active:
            cycle = " -> ".join((*active, function))
            raise StackAnalysisError(f"recursive call graph is unbounded: {cycle}")

        entry = self.entries.get(function)
        if entry is None:
            result = PathEstimate(
                self.external_chain_reserve,
                (function, "<external-call-chain-reserve>"),
                (function,),
            )
            self._memo[function] = result
            return result

        own_bytes = entry.bytes_used
        own_assumptions: tuple[str, ...] = ()

        best = PathEstimate(own_bytes, (function,), own_assumptions)
        node = self.graph.get(function, CallNode())
        next_active = (*active, function)

        for callee in sorted(node.calls):
            child = self._estimate(callee, next_active)
            candidate = PathEstimate(
                own_bytes + child.bytes_used,
                (function, *child.path),
                tuple(sorted(set((*own_assumptions, *child.assumed_frames)))),
                child.indirect_callers,
            )
            if candidate.bytes_used > best.bytes_used:
                best = candidate

        for callee in sorted(node.tail_calls):
            child = self._estimate(callee, next_active)
            candidate = PathEstimate(
                max(own_bytes, child.bytes_used),
                (function, "[tail]", *child.path),
                tuple(sorted(set((*own_assumptions, *child.assumed_frames)))),
                child.indirect_callers,
            )
            if candidate.bytes_used > best.bytes_used:
                best = candidate

        if node.has_indirect_call:
            candidate = PathEstimate(
                own_bytes + self.indirect_reserve,
                (function, "<indirect-call-reserve>"),
                own_assumptions,
                (function,),
            )
            if candidate.bytes_used > best.bytes_used:
                best = candidate

        self._memo[function] = best
        return best


def require_nonnegative_int(config: dict[str, Any], key: str) -> int:
    value = config.get(key)
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise StackAnalysisError(f"{key} must be a non-negative integer")
    return value


def require_context(value: Any, label: str, interrupt: bool) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise StackAnalysisError(f"{label} must be an object")
    name = value.get("name")
    root = value.get("root")
    if not isinstance(name, str) or not name:
        raise StackAnalysisError(f"{label}.name must be a non-empty string")
    if not isinstance(root, str) or not root:
        raise StackAnalysisError(f"{label}.root must be a non-empty string")
    if interrupt:
        priority_macro = value.get("priority_macro")
        if not isinstance(priority_macro, str) or not priority_macro:
            raise StackAnalysisError(
                f"{label}.priority_macro must be a non-empty string"
            )
    return value


def analyze(
    entries: dict[str, StackEntry],
    graph: dict[str, CallNode],
    config: dict[str, Any],
    budget: int,
    priority_macros: dict[str, int],
) -> dict[str, Any]:
    if budget < 0:
        raise StackAnalysisError("linker stack budget must be non-negative")
    external_chain_reserve = require_nonnegative_int(
        config, "external_call_chain_reserve_bytes"
    )
    indirect_reserve = require_nonnegative_int(
        config, "indirect_call_reserve_bytes"
    )
    exception_frame = require_nonnegative_int(config, "exception_frame_bytes")
    minimum_margin = require_nonnegative_int(config, "minimum_margin_bytes")
    main = require_context(config.get("main_context"), "main_context", False)
    interrupt_values = config.get("interrupt_contexts")
    if not isinstance(interrupt_values, list):
        raise StackAnalysisError("interrupt_contexts must be an array")
    interrupts = [
        require_context(value, f"interrupt_contexts[{index}]", True)
        for index, value in enumerate(interrupt_values)
    ]
    resolved_interrupts: list[dict[str, Any]] = []
    for context in interrupts:
        macro = context["priority_macro"]
        if macro not in priority_macros:
            raise StackAnalysisError(f"IRQ priority macro is absent: {macro}")
        resolved_interrupts.append(
            {**context, "priority": priority_macros[macro]}
        )
    priorities = [value["priority"] for value in resolved_interrupts]
    if len(set(priorities)) != len(priorities):
        raise StackAnalysisError("interrupt priorities must be unique")

    estimator = StackEstimator(
        entries, graph, external_chain_reserve, indirect_reserve
    )
    main_estimate = estimator.estimate(main["root"])
    context_results: list[dict[str, Any]] = []
    for context in sorted(
        resolved_interrupts, key=lambda item: item["priority"], reverse=True
    ):
        estimate = estimator.estimate(context["root"])
        context_results.append(
            {
                "name": context["name"],
                "root": context["root"],
                "priority": context["priority"],
                "priority_macro": context["priority_macro"],
                "software_bytes": estimate.bytes_used,
                "exception_frame_bytes": exception_frame,
                "path": list(estimate.path),
                "assumed_frames": list(estimate.assumed_frames),
                "indirect_callers": list(estimate.indirect_callers),
            }
        )

    envelope = main_estimate.bytes_used + sum(
        context["software_bytes"] + exception_frame
        for context in context_results
    )
    largest_frames = sorted(
        entries.values(), key=lambda entry: (-entry.bytes_used, entry.name)
    )[:10]
    ignored_inline_asm = sorted(
        entry.name
        for entry in entries.values()
        if "ignoring_inline_asm" in entry.qualifiers
    )
    dynamic_frames = sorted(
        entry.name for entry in entries.values() if "dynamic" in entry.qualifiers
    )
    if dynamic_frames:
        raise StackAnalysisError(
            "dynamic stack frames are not bounded: " + ", ".join(dynamic_frames)
        )

    assumed_frames = set(main_estimate.assumed_frames)
    indirect_callers = set(main_estimate.indirect_callers)
    for context in context_results:
        assumed_frames.update(context["assumed_frames"])
        indirect_callers.update(context["indirect_callers"])

    return {
        "budget_bytes": budget,
        "estimated_envelope_bytes": envelope,
        "required_budget_bytes": envelope + minimum_margin,
        "margin_bytes": budget - envelope,
        "minimum_margin_bytes": minimum_margin,
        "margin_after_policy_bytes": budget - envelope - minimum_margin,
        "passed": envelope + minimum_margin <= budget,
        "function_count": len(entries),
        "disassembled_function_count": len(graph),
        "external_call_chain_reserve_bytes": external_chain_reserve,
        "indirect_call_reserve_bytes": indirect_reserve,
        "exception_frame_bytes": exception_frame,
        "main_context": {
            "name": main["name"],
            "root": main["root"],
            "software_bytes": main_estimate.bytes_used,
            "path": list(main_estimate.path),
            "assumed_frames": list(main_estimate.assumed_frames),
            "indirect_callers": list(main_estimate.indirect_callers),
        },
        "interrupt_contexts": context_results,
        "largest_frames": [
            {
                "name": entry.name,
                "bytes": entry.bytes_used,
                "source": entry.source,
                "qualifiers": list(entry.qualifiers),
            }
            for entry in largest_frames
        ],
        "assumed_frames": sorted(assumed_frames),
        "indirect_callers": sorted(indirect_callers),
        "ignoring_inline_asm": ignored_inline_asm,
    }


def render_text(report: dict[str, Any]) -> str:
    status = "PASS" if report["passed"] else "FAIL"
    lines = [
        "Stack Usage Report",
        "==================",
        f"Result: {status}",
        f"Reserved stack: {report['budget_bytes']} bytes",
        f"Conservative preemption envelope: "
        f"{report['estimated_envelope_bytes']} bytes",
        f"Margin: {report['margin_bytes']} bytes",
        f"Required safety margin: {report['minimum_margin_bytes']} bytes",
        f"Minimum required reserve: {report['required_budget_bytes']} bytes",
        f"Margin after policy: {report['margin_after_policy_bytes']} bytes",
        "",
        "Execution contexts",
        "------------------",
    ]
    main = report["main_context"]
    lines.append(
        f"main: {main['software_bytes']} bytes :: {' -> '.join(main['path'])}"
    )
    for context in report["interrupt_contexts"]:
        total = context["software_bytes"] + context["exception_frame_bytes"]
        lines.append(
            f"priority {context['priority']:>2} {context['name']}: {total} bytes "
            f"({context['software_bytes']} software + "
            f"{context['exception_frame_bytes']} exception) :: "
            f"{' -> '.join(context['path'])}"
        )

    lines.extend(
        ["", "Largest compiler-reported frames", "--------------------------------"]
    )
    for entry in report["largest_frames"]:
        lines.append(
            f"{entry['bytes']:>4} bytes  {entry['name']}  ({entry['source']})"
        )

    lines.extend(
        [
            "",
            "Coverage and assumptions",
            "------------------------",
            f".su functions: {report['function_count']}",
            f"disassembled functions: {report['disassembled_function_count']}",
            f"external/missing call-chain reserve: "
            f"{report['external_call_chain_reserve_bytes']} bytes per boundary",
            f"indirect-call reserve: {report['indirect_call_reserve_bytes']} bytes",
            f"compiler entries marked ignoring_inline_asm: "
            f"{len(report['ignoring_inline_asm'])}",
            "worst-path assumed frames: "
            + (", ".join(report["assumed_frames"]) or "none"),
            "worst-path indirect callers: "
            + (", ".join(report["indirect_callers"]) or "none"),
            "",
            "The envelope assumes every configured IRQ preempts the next lower-urgency",
            "context at its deepest software call path. The exception reserve includes",
            "the Cortex-M7 extended floating-point frame plus one alignment word.",
        ]
    )
    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--su-dir", required=True, type=Path)
    parser.add_argument("--disassembly", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--linker-script", required=True, type=Path)
    parser.add_argument("--irq-policy-header", required=True, type=Path)
    parser.add_argument("--text-output", required=True, type=Path)
    parser.add_argument("--json-output", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        config = json.loads(args.config.read_text(encoding="utf-8"))
        if not isinstance(config, dict):
            raise StackAnalysisError("configuration root must be an object")
        report = analyze(
            parse_stack_usage(args.su_dir),
            parse_disassembly(args.disassembly),
            config,
            parse_linker_stack_budget(args.linker_script),
            parse_integer_macros(args.irq_policy_header),
        )
        text_report = render_text(report)
        args.text_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.text_output.write_text(text_report, encoding="utf-8")
        args.json_output.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(text_report, end="")
        return 0 if report["passed"] else 1
    except (OSError, json.JSONDecodeError, StackAnalysisError) as error:
        print(f"stack usage analysis failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
