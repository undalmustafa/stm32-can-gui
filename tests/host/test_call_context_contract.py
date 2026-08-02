"""Validate standardized call-context declarations in firmware headers."""

from __future__ import annotations

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
HEADER_DIR = ROOT / "Core" / "Inc"
EXCLUDED_HEADERS = {
    "can_protocol_generated.h",
    "main.h",
    "stm32h7xx_hal_conf.h",
    "stm32h7xx_it.h",
    "stm32h7xx_nucleo_conf.h",
}
VALID_DEFAULTS = {"MAIN_LOOP_ONLY", "ISR_SAFE", "INTERNAL"}
CONTRACT_PATTERN = re.compile(
    r"CALL_CONTEXT_DEFAULT:\s*([^\s*]+).*?"
    r"CALL_CONTEXT_ISR_SAFE:\s*([^\r\n*]+).*?"
    r"CALL_CONTEXT_INTERNAL:\s*([^\r\n*]+)",
    re.DOTALL,
)
ISR_NAME_PATTERN = re.compile(r"\b([A-Za-z_]\w*(?:FromIsr|RecordIsr))\s*\(")


def parse_names(value: str, label: str, header: Path) -> set[str]:
    items = {item.strip() for item in value.split(",") if item.strip()}
    if not items:
        raise AssertionError(f"{header.name}: empty {label} list")
    if "none" in items or "all" in items:
        if len(items) != 1:
            raise AssertionError(
                f"{header.name}: {label} cannot combine all/none with symbols"
            )
        return items
    for item in items:
        if re.fullmatch(r"[A-Za-z_]\w*", item) is None:
            raise AssertionError(
                f"{header.name}: invalid {label} symbol {item!r}"
            )
    return items


def validate_header(header: Path) -> None:
    text = header.read_text(encoding="utf-8")
    matches = CONTRACT_PATTERN.findall(text)
    if len(matches) != 1:
        raise AssertionError(
            f"{header.name}: expected exactly one call-context contract"
        )

    default, isr_value, internal_value = matches[0]
    if default not in VALID_DEFAULTS:
        raise AssertionError(f"{header.name}: invalid default context {default}")
    isr_names = parse_names(isr_value, "ISR_SAFE", header)
    internal_names = parse_names(internal_value, "INTERNAL", header)

    if default == "INTERNAL" and internal_names != {"all"}:
        raise AssertionError(
            f"{header.name}: INTERNAL default requires INTERNAL: all"
        )
    if default != "INTERNAL" and internal_names == {"all"}:
        raise AssertionError(
            f"{header.name}: INTERNAL: all requires INTERNAL default"
        )

    if default != "ISR_SAFE" and isr_names not in ({"all"}, {"none"}):
        invalid_isr_names = {
            name
            for name in isr_names
            if not (name.endswith("FromIsr") or name.endswith("RecordIsr"))
        }
        if invalid_isr_names:
            raise AssertionError(
                f"{header.name}: ISR exception lacks FromIsr/RecordIsr suffix: "
                + ", ".join(sorted(invalid_isr_names))
            )

    for name in (isr_names | internal_names) - {"all", "none"}:
        if re.search(rf"\b{re.escape(name)}\s*\(", text) is None:
            raise AssertionError(
                f"{header.name}: contract symbol is not declared: {name}"
            )

    declared_isr_names = set(ISR_NAME_PATTERN.findall(text))
    if default == "ISR_SAFE" or isr_names == {"all"}:
        missing_isr_names: set[str] = set()
    else:
        missing_isr_names = declared_isr_names - isr_names
    if missing_isr_names:
        raise AssertionError(
            f"{header.name}: ISR-named API is not ISR_SAFE: "
            + ", ".join(sorted(missing_isr_names))
        )


def main() -> int:
    headers = sorted(
        path
        for path in HEADER_DIR.glob("*.h")
        if path.name not in EXCLUDED_HEADERS
    )
    if not headers:
        raise AssertionError("no project module headers found")
    for header in headers:
        validate_header(header)
    print(f"PASS: call-context contracts validated for {len(headers)} headers")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, AssertionError) as error:
        print(f"call-context contract failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
