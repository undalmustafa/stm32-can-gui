"""Host tests for the GCC stack-usage and call-graph analyzer."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts" / "stack_usage_report.py"
SPEC = importlib.util.spec_from_file_location("stack_usage_report", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("stack usage report module could not be loaded")
stack_usage_report = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = stack_usage_report
SPEC.loader.exec_module(stack_usage_report)


class StackUsageReportTests(unittest.TestCase):
    def test_parses_su_records_and_keeps_largest_duplicate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "one.su").write_text(
                "Core/Src/a.c:10:3:worker.constprop\t24\tstatic\n"
                "Core/Src/a.c:20:3:callback\t16\tstatic\n",
                encoding="utf-8",
            )
            (root / "two.su").write_text(
                "Drivers/a.c:30:3:callback\t40\tstatic,ignoring_inline_asm\n",
                encoding="utf-8",
            )

            entries = stack_usage_report.parse_stack_usage(root)

        self.assertEqual(entries["worker.constprop"].bytes_used, 24)
        self.assertEqual(entries["callback"].bytes_used, 40)
        self.assertIn("ignoring_inline_asm", entries["callback"].qualifiers)

    def test_parses_direct_tail_and_indirect_calls(self) -> None:
        listing = """
08000000 <root>:
 8000000: f000 f800  bl 8000010 <child.constprop.0>
 8000004: 4798       blx r3
08000008 <tailer>:
 8000008: f000 b800  b.w 8000020 <leaf>
08000010 <child.constprop.0>:
 8000010: 4770       bx lr
08000020 <leaf>:
 8000020: 4770       bx lr
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.list"
            path.write_text(listing, encoding="utf-8")
            graph = stack_usage_report.parse_disassembly(path)

        self.assertEqual(graph["root"].calls, {"child.constprop"})
        self.assertTrue(graph["root"].has_indirect_call)
        self.assertEqual(graph["tailer"].tail_calls, {"leaf"})

    def test_parses_linker_budget_and_irq_priority_macros(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            linker = root / "firmware.ld"
            header = root / "irq_policy.h"
            linker.write_text("_Min_Stack_Size = 0x400;\n", encoding="utf-8")
            header.write_text(
                "#define APP_IRQ_PRIORITY_HIGH 1U\n"
                "#define APP_IRQ_PRIORITY_LOW 15UL\n",
                encoding="utf-8",
            )

            budget = stack_usage_report.parse_linker_stack_budget(linker)
            macros = stack_usage_report.parse_integer_macros(header)

        self.assertEqual(budget, 1024)
        self.assertEqual(macros["APP_IRQ_PRIORITY_HIGH"], 1)
        self.assertEqual(macros["APP_IRQ_PRIORITY_LOW"], 15)

    def test_estimator_accumulates_calls_but_not_tail_frames(self) -> None:
        entry = stack_usage_report.StackEntry
        entries = {
            "root": entry("root", 20, ("static",), "root.c"),
            "child": entry("child", 30, ("static",), "child.c"),
            "tailer": entry("tailer", 24, ("static",), "tailer.c"),
            "leaf": entry("leaf", 40, ("static",), "leaf.c"),
        }
        node = stack_usage_report.CallNode
        graph = {
            "root": node(calls={"child"}),
            "child": node(),
            "tailer": node(tail_calls={"leaf"}),
            "leaf": node(),
        }
        estimator = stack_usage_report.StackEstimator(entries, graph, 64, 80)

        self.assertEqual(estimator.estimate("root").bytes_used, 50)
        self.assertEqual(estimator.estimate("tailer").bytes_used, 40)

    def test_unknown_and_indirect_calls_receive_conservative_reserves(self) -> None:
        entry = stack_usage_report.StackEntry
        entries = {"root": entry("root", 24, ("static",), "root.c")}
        graph = {
            "root": stack_usage_report.CallNode(
                calls={"external_leaf"}, has_indirect_call=True
            ),
            "external_leaf": stack_usage_report.CallNode(
                calls={"deeper_external"}
            ),
        }
        estimator = stack_usage_report.StackEstimator(entries, graph, 96, 128)

        estimate = estimator.estimate("root")

        self.assertEqual(estimate.bytes_used, 152)
        self.assertEqual(estimate.indirect_callers, ("root",))

    def test_analysis_sums_nested_irq_contexts_and_applies_budget(self) -> None:
        entry = stack_usage_report.StackEntry
        entries = {
            "main": entry("main", 100, ("static",), "main.c"),
            "low_irq": entry("low_irq", 40, ("static",), "irq.c"),
            "high_irq": entry("high_irq", 20, ("static",), "irq.c"),
        }
        graph = {name: stack_usage_report.CallNode() for name in entries}
        config = {
            "external_call_chain_reserve_bytes": 64,
            "indirect_call_reserve_bytes": 64,
            "exception_frame_bytes": 36,
            "minimum_margin_bytes": 8,
            "main_context": {"name": "main", "root": "main"},
            "interrupt_contexts": [
                {
                    "name": "high",
                    "root": "high_irq",
                    "priority_macro": "IRQ_HIGH",
                },
                {
                    "name": "low",
                    "root": "low_irq",
                    "priority_macro": "IRQ_LOW",
                },
            ],
        }

        report = stack_usage_report.analyze(
            entries, graph, config, 240, {"IRQ_HIGH": 1, "IRQ_LOW": 3}
        )

        self.assertEqual(report["estimated_envelope_bytes"], 232)
        self.assertEqual(report["required_budget_bytes"], 240)
        self.assertEqual(report["margin_bytes"], 8)
        self.assertEqual(report["margin_after_policy_bytes"], 0)
        self.assertTrue(report["passed"])
        self.assertEqual(
            [context["name"] for context in report["interrupt_contexts"]],
            ["low", "high"],
        )

        config["minimum_margin_bytes"] = 9
        report = stack_usage_report.analyze(
            entries, graph, config, 240, {"IRQ_HIGH": 1, "IRQ_LOW": 3}
        )
        self.assertFalse(report["passed"])

    def test_recursive_graph_is_rejected(self) -> None:
        entry = stack_usage_report.StackEntry
        entries = {
            "a": entry("a", 8, ("static",), "a.c"),
            "b": entry("b", 8, ("static",), "b.c"),
        }
        graph = {
            "a": stack_usage_report.CallNode(calls={"b"}),
            "b": stack_usage_report.CallNode(calls={"a"}),
        }
        estimator = stack_usage_report.StackEstimator(entries, graph, 64, 64)

        with self.assertRaisesRegex(
            stack_usage_report.StackAnalysisError, "recursive call graph"
        ):
            estimator.estimate("a")


if __name__ == "__main__":
    unittest.main(verbosity=2)
