"""Check that boot/application linker profiles match the frozen flash map."""

from __future__ import annotations

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]


def assignment(text: str, symbol: str) -> int:
    match = re.search(rf"^{re.escape(symbol)}\s*=\s*(0x[0-9A-Fa-f]+);", text,
                      re.MULTILINE)
    if match is None:
        raise AssertionError(f"missing hexadecimal assignment for {symbol}")
    return int(match.group(1), 16)


def validate_profile(path: Path, origin: int, length: int) -> None:
    text = path.read_text(encoding="utf-8")
    if "INCLUDE STM32H7A3ZITXQ_FLASH.ld" not in text:
        raise AssertionError(f"{path.name}: shared linker script is not included")
    if assignment(text, "__firmware_flash_origin__") != origin:
        raise AssertionError(f"{path.name}: unexpected flash origin")
    if assignment(text, "__firmware_flash_length__") != length:
        raise AssertionError(f"{path.name}: unexpected flash length")


def main() -> int:
    validate_profile(
        ROOT / "linker/STM32H7A3ZITXQ_BOOT.ld", 0x08000000, 0x00020000
    )
    validate_profile(
        ROOT / "linker/STM32H7A3ZITXQ_SLOT_A.ld", 0x08020400, 0x000DFC00
    )
    validate_profile(
        ROOT / "linker/STM32H7A3ZITXQ_SLOT_B.ld", 0x08100400, 0x000DFC00
    )

    slot_a = (ROOT / "linker/STM32H7A3ZITXQ_SLOT_A.ld").read_text(
        encoding="utf-8"
    )
    slot_b = (ROOT / "linker/STM32H7A3ZITXQ_SLOT_B.ld").read_text(
        encoding="utf-8"
    )
    if assignment(slot_a, "__image_slot_base__") != 0x08020000:
        raise AssertionError("slot A manifest base changed")
    if assignment(slot_b, "__image_slot_base__") != 0x08100000:
        raise AssertionError("slot B manifest base changed")

    shared = (ROOT / "STM32H7A3ZITXQ_FLASH.ld").read_text(encoding="utf-8")
    if "__vector_table_start__ = .;" not in shared:
        raise AssertionError("shared linker script does not export vector start")
    if "ADDR(.isr_vector) == ORIGIN(FLASH)" not in shared:
        raise AssertionError("vector-at-origin linker assertion is missing")
    if "SIZEOF(.isr_vector) <= 0x400" not in shared:
        raise AssertionError("vector table size assertion is missing")

    startup = next((ROOT / "Core/Startup").glob("startup_*.s")).read_text(
        encoding="utf-8"
    )
    vector_body = startup.split("g_pfnVectors:", 1)[1].split(
        ".size  g_pfnVectors", 1
    )[0]
    vector_words = len(re.findall(r"^\s*\.word\s+", vector_body, re.MULTILINE))
    if not 128 < vector_words <= 256:
        raise AssertionError(
            f"unexpected vector table size: {vector_words} words"
        )

    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    for profile in ("slot-a", "slot-b"):
        if f"IMAGE_LAYOUT={profile}" not in makefile:
            raise AssertionError(f"make profile {profile} is missing")

    print("PASS: boot and A/B linker contracts match the flash map")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, AssertionError) as error:
        print(f"boot linker contract failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
