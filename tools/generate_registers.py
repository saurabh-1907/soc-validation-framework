#!/usr/bin/env python3
"""Turn a databook register map (YAML) into a header-only C++17 access layer.

The generated code carries the access policy in its *type surface* rather than
in comments or runtime checks:

  RW  field -> getter + setter
  RO  field -> getter only          -> writing it is a compile error
  W1C field -> getter + clear_X()   -> "write 1 to clear" is the only mutation
                                        offered, so nobody writes 0 and wonders
                                        why the bit did not change

Each register is a value type holding one 32-bit word. Field accessors read and
write that cached word, so touching six fields still costs exactly one bus
transaction - see RegisterAccessor::Read/Modify in include/soc/register.hpp.
"""

from __future__ import annotations

import argparse
import pathlib
import sys

import yaml

HEADER = """// GENERATED FILE - DO NOT EDIT.
// Produced by tools/generate_registers.py from {src}
// Regenerate with: python3 tools/generate_registers.py {src} -o {out}
#pragma once

#include <cstdint>
#include "soc/register.hpp"

namespace soc::regs {{
"""

FOOTER = """
}  // namespace soc::regs
"""


def parse_bits(spec: str) -> tuple[int, int]:
    """'11:4' -> (11, 4);  '0' -> (0, 0)."""
    text = str(spec)
    if ":" in text:
        hi, lo = (int(p) for p in text.split(":"))
    else:
        hi = lo = int(text)
    if hi < lo:
        raise ValueError(f"bit range {spec!r} has hi < lo")
    return hi, lo


def mask(hi: int, lo: int) -> int:
    return ((1 << (hi - lo + 1)) - 1) << lo


def emit_field(field: dict) -> list[str]:
    name = field["name"]
    hi, lo = parse_bits(field["bits"])
    access = field.get("access", "RW").upper()
    m = mask(hi, lo)
    width = hi - lo + 1
    out: list[str] = []

    if desc := field.get("description"):
        out.append(f"    // {desc}")
    out.append(f"    // bits [{hi}:{lo}] {access}")

    if enum := field.get("enum"):
        out.append(f"    enum class {name}_e : uint32_t {{")
        out.extend(f"      k{k} = {v}," for k, v in enum.items())
        out.append("    };")

    out.append(
        f"    [[nodiscard]] constexpr uint32_t {name}() const noexcept "
        f"{{ return (raw & 0x{m:08X}u) >> {lo}; }}"
    )

    if access == "RW":
        out.append(
            f"    constexpr auto& set_{name}(uint32_t v) noexcept {{ "
            f"raw = (raw & ~0x{m:08X}u) | ((v << {lo}) & 0x{m:08X}u); return *this; }}"
        )
    elif access == "W1C":
        # No setter at all: the only legal mutation of a W1C bit is writing a 1
        # to clear it. Offering set_X(0) would invite code that looks correct
        # and does nothing.
        out.append(
            f"    constexpr auto& clear_{name}() noexcept "
            f"{{ raw |= 0x{m:08X}u; return *this; }}  // W1C: write 1 to clear"
        )
    # RO: getter only. Attempting to write is a compile error.

    out.append(f"    static constexpr uint32_t k{name}Mask  = 0x{m:08X}u;")
    out.append(f"    static constexpr uint32_t k{name}Shift = {lo}u;")
    out.append(f"    static constexpr uint32_t k{name}Width = {width}u;")
    return out


def emit_register(block: dict, reg: dict) -> list[str]:
    block_name, reg_name = block["name"], reg["name"]
    addr = int(block["base"]) + int(reg["offset"])
    access = reg.get("access", "RW").upper()

    lines = [
        "",
        f"  // {block_name}.{reg_name} @ 0x{addr:08X}"
        + (f" - {reg['description']}" if reg.get("description") else ""),
        f"  struct {block_name}_{reg_name} {{",
        f"    static constexpr uint64_t kAddress = 0x{addr:08X}ull;",
        f"    static constexpr uint32_t kReset   = 0x{int(reg['reset']):08X}u;",
        f"    static constexpr Access   kAccess  = Access::{access};",
        f'    static constexpr const char* kName = "{block_name}.{reg_name}";',
        "",
        "    uint32_t raw{kReset};",
        "",
        "    constexpr explicit operator uint32_t() const noexcept { return raw; }",
    ]
    for field in reg.get("fields", []):
        lines.append("")
        lines.extend(emit_field(field))
    lines.append("  };")
    return lines


def generate(spec: dict, src: str, out: str) -> str:
    lines = [HEADER.format(src=src, out=out)]
    for block in spec["blocks"]:
        lines.append(f"\n  // ===== block {block['name']} @ 0x{int(block['base']):08X} =====")
        for reg in block["registers"]:
            lines.extend(emit_register(block, reg))
    lines.append(FOOTER)
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("spec", type=pathlib.Path)
    ap.add_argument("-o", "--output", type=pathlib.Path, required=True)
    args = ap.parse_args()

    spec = yaml.safe_load(args.spec.read_text(encoding="utf-8"))
    if not spec or "blocks" not in spec:
        print(f"error: {args.spec} has no 'blocks'", file=sys.stderr)
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        generate(spec, args.spec.name, args.output.name), encoding="utf-8"
    )

    n_regs = sum(len(b["registers"]) for b in spec["blocks"])
    print(f"generated {args.output} ({len(spec['blocks'])} blocks, {n_regs} registers)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
