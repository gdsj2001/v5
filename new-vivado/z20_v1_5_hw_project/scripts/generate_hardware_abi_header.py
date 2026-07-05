#!/usr/bin/env python3
"""Generate the board-software hardware ABI header from XSA and register maps."""

from __future__ import annotations

import argparse
import difflib
import re
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable
from xml.etree import ElementTree


PROJECT_ROOT_PREFIX = "new-vivado/z20_v1_5_hw_project"


@dataclass(frozen=True)
class MemRange:
    instance: str
    bus: str
    base: int
    high: int

    @property
    def size(self) -> int:
        return self.high - self.base + 1


@dataclass(frozen=True)
class Register:
    offset: int
    name: str
    reset_value: int | None = None
    description_value: int | None = None


@dataclass(frozen=True)
class AxisSection:
    base: int
    stride: int
    registers: list[Register]


def fail(message: str) -> None:
    raise SystemExit(f"ERROR: {message}")


def rel_project_path(project_dir: Path, path: Path) -> str:
    try:
        rel = path.resolve().relative_to(project_dir.resolve()).as_posix()
    except ValueError as exc:
        raise ValueError(f"path is outside project: {path}") from exc
    return f"{PROJECT_ROOT_PREFIX}/{rel}"


def read_text(path: Path) -> str:
    if not path.exists():
        fail(f"missing file: {path}")
    return path.read_text(encoding="utf-8-sig", errors="replace")


def parse_hex(value: str) -> int:
    return int(value.replace("_", ""), 16)


def parse_xsa_memranges(xsa_path: Path) -> dict[tuple[str, str], MemRange]:
    if not xsa_path.exists():
        fail(f"missing XSA: {xsa_path}")
    if not zipfile.is_zipfile(xsa_path):
        fail(f"XSA is not a zip container: {xsa_path}")

    with zipfile.ZipFile(xsa_path) as archive:
        hwh_names = [name for name in archive.namelist() if name.lower().endswith(".hwh")]
        if not hwh_names:
            fail("XSA does not contain a .hwh file")
        hwh_name = "system.hwh" if "system.hwh" in hwh_names else sorted(hwh_names)[0]
        root = ElementTree.fromstring(archive.read(hwh_name))

    ranges: dict[tuple[str, str], MemRange] = {}
    for elem in root.iter():
        if elem.tag.split("}")[-1] != "MEMRANGE":
            continue
        attrs = {key.upper(): value for key, value in elem.attrib.items()}
        instance = attrs.get("INSTANCE")
        bus = attrs.get("SLAVEBUSINTERFACE")
        base = attrs.get("BASEVALUE")
        high = attrs.get("HIGHVALUE")
        if not instance or not bus or not base or not high:
            continue
        mem = MemRange(instance=instance, bus=bus, base=parse_hex(base), high=parse_hex(high))
        ranges[(instance.lower(), bus.lower())] = mem
    return ranges


def expected_memrange(
    ranges: dict[tuple[str, str], MemRange], instance: str, bus: str
) -> MemRange:
    key = (instance.lower(), bus.lower())
    if key not in ranges:
        available = ", ".join(f"{inst}/{iface}" for inst, iface in sorted(ranges))
        fail(f"XSA missing MEMRANGE for {instance}/{bus}; available: {available}")
    return ranges[key]


def strip_cell(cell: str) -> str:
    return cell.strip().strip("`").strip()


def split_md_row(line: str) -> list[str]:
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def is_separator_row(cells: Iterable[str]) -> bool:
    return all(re.fullmatch(r":?-{3,}:?", cell.strip()) for cell in cells)


def find_heading_index(lines: list[str], heading_prefix: str) -> int:
    prefix = heading_prefix.strip().lower()
    for index, line in enumerate(lines):
        if line.strip().lower().startswith(prefix):
            return index
    fail(f"missing heading: {heading_prefix}")
    return -1


def first_table_after_heading(text: str, heading_prefix: str) -> list[list[str]]:
    lines = text.splitlines()
    start = find_heading_index(lines, heading_prefix)
    table_lines: list[str] = []
    for line in lines[start + 1 :]:
        stripped = line.strip()
        if stripped.startswith("#") and table_lines:
            break
        if stripped.startswith("|"):
            table_lines.append(stripped)
        elif table_lines:
            break
    if not table_lines:
        fail(f"missing Markdown table after heading: {heading_prefix}")

    rows: list[list[str]] = []
    for line in table_lines:
        cells = split_md_row(line)
        if cells and not is_separator_row(cells):
            rows.append(cells)
    if len(rows) < 2:
        fail(f"Markdown table has no data rows after heading: {heading_prefix}")
    return rows


def first_hex_in(text: str) -> int | None:
    match = re.search(r"0x[0-9A-Fa-f_]+", text)
    return parse_hex(match.group(0)) if match else None


def parse_register_table(text: str, heading_prefix: str) -> list[Register]:
    rows = first_table_after_heading(text, heading_prefix)
    header = [strip_cell(cell).lower() for cell in rows[0]]
    offset_idx = 0
    name_idx = 1
    reset_idx = next((i for i, cell in enumerate(header) if "reset" in cell), None)
    desc_idx = next((i for i, cell in enumerate(header) if "description" in cell), None)

    registers: list[Register] = []
    seen_offsets: dict[int, str] = {}
    for row in rows[1:]:
        if len(row) <= max(offset_idx, name_idx):
            continue
        offset_text = strip_cell(row[offset_idx])
        name = strip_cell(row[name_idx])
        offset_match = re.search(r"\+?0x[0-9A-Fa-f_]+", offset_text)
        if not offset_match or not name:
            continue
        offset = parse_hex(offset_match.group(0).replace("+", ""))
        if offset in seen_offsets:
            fail(
                f"duplicate offset 0x{offset:08X} in {heading_prefix}: "
                f"{seen_offsets[offset]} and {name}"
            )
        seen_offsets[offset] = name
        reset_value = first_hex_in(row[reset_idx]) if reset_idx is not None and reset_idx < len(row) else None
        description_value = (
            first_hex_in(row[desc_idx]) if desc_idx is not None and desc_idx < len(row) else None
        )
        registers.append(Register(offset=offset, name=name, reset_value=reset_value, description_value=description_value))
    if not registers:
        fail(f"no registers parsed after heading: {heading_prefix}")
    return registers


def parse_axis_section(text: str, heading_prefix: str, formula_pattern: str) -> AxisSection:
    lines = text.splitlines()
    start = find_heading_index(lines, heading_prefix)
    section_lines: list[str] = []
    for line in lines[start :]:
        if line.strip().startswith("#") and section_lines and line != lines[start]:
            break
        section_lines.append(line)
    section_text = "\n".join(section_lines)
    match = re.search(formula_pattern, section_text, re.IGNORECASE)
    if not match:
        fail(f"missing base/stride formula in {heading_prefix}")
    base = parse_hex(match.group(1))
    stride = parse_hex(match.group(2))
    registers = parse_register_table(text, heading_prefix)
    return AxisSection(base=base, stride=stride, registers=registers)


def parse_doc_base(text: str, label: str) -> int:
    pattern = rf"{re.escape(label)}[^\n`]*(?:`)?(0x[0-9A-Fa-f_]+)"
    match = re.search(pattern, text, re.IGNORECASE)
    if not match:
        fail(f"missing documented base label: {label}")
    return parse_hex(match.group(1))


def parse_doc_range(text: str) -> int:
    match = re.search(r"Range:\s*`?64K`?", text, re.IGNORECASE)
    if match:
        return 0x10000
    match = re.search(r"range\s*`?64K`?", text, re.IGNORECASE)
    if match:
        return 0x10000
    fail("missing documented 64K range")
    return 0


def sanitize_macro_name(name: str) -> str:
    sanitized = re.sub(r"[^A-Za-z0-9]+", "_", name.upper()).strip("_")
    sanitized = re.sub(r"_+", "_", sanitized)
    if not sanitized:
        fail(f"empty macro name from register name: {name!r}")
    if sanitized[0].isdigit():
        sanitized = f"R_{sanitized}"
    return sanitized


def find_register(registers: list[Register], name: str) -> Register:
    for reg in registers:
        if reg.name == name:
            return reg
    fail(f"missing required register: {name}")
    raise AssertionError


def value_for(reg: Register, source: str) -> int:
    if reg.reset_value is not None:
        return reg.reset_value
    if reg.description_value is not None:
        return reg.description_value
    fail(f"missing value for {source}.{reg.name}")
    raise AssertionError


def c_u32(value: int) -> str:
    return f"UINT32_C(0x{value:08X})"


def emit_reg_offsets(lines: list[str], prefix: str, registers: list[Register]) -> None:
    for reg in registers:
        macro = sanitize_macro_name(reg.name)
        lines.append(f"#define {prefix}_{macro}_OFFSET {c_u32(reg.offset)}")


def emit_reg_addrs(lines: list[str], base_macro: str, prefix: str, registers: list[Register]) -> None:
    for reg in registers:
        macro = sanitize_macro_name(reg.name)
        lines.append(f"#define {prefix}_{macro}_ADDR ({base_macro} + {prefix}_{macro}_OFFSET)")


def generate_header(
    project_dir: Path,
    xsa_path: Path,
    blocks: dict[str, MemRange],
    step_global: list[Register],
    step_axis: AxisSection,
    step_debug: AxisSection,
    step_live: AxisSection,
    estop_regs: list[Register],
    io_regs: list[Register],
) -> str:
    lines: list[str] = []
    lines.append("/*")
    lines.append(" * Auto-generated hardware ABI header for Z20 v1.5.")
    lines.append(" * Do not edit this file by hand.")
    lines.append(f" * Generator: {PROJECT_ROOT_PREFIX}/scripts/generate_hardware_abi_header.py")
    lines.append(f" * Source XSA: {rel_project_path(project_dir, xsa_path)}")
    lines.append(f" * Source register map: {PROJECT_ROOT_PREFIX}/ip_repo/axi_stepdir_enc_v2/step_ip_register_map.md")
    lines.append(f" * Source register map: {PROJECT_ROOT_PREFIX}/board_inputs/pl_estop_register_map.md")
    lines.append(f" * Source register map: {PROJECT_ROOT_PREFIX}/docs/io_owner_register_map.md")
    lines.append(" */")
    lines.append("")
    lines.append("#ifndef V3_HARDWARE_ABI_H")
    lines.append("#define V3_HARDWARE_ABI_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("#define V3_HW_ABI_CONTRACT_VERSION UINT32_C(0x00010000)")
    lines.append("#define V3_HW_ABI_AXIS_COUNT UINT32_C(8)")
    lines.append("")
    lines.append("/* AXI address ranges from the current XSA. */")
    for macro, key in (
        ("V3_HW_STEP_IP", "step_ip"),
        ("V3_HW_PL_ESTOP", "pl_estop"),
        ("V3_HW_IO_OWNER", "io_owner"),
    ):
        mem = blocks[key]
        lines.append(f"#define {macro}_BASE_ADDR {c_u32(mem.base)}")
        lines.append(f"#define {macro}_HIGH_ADDR {c_u32(mem.high)}")
        lines.append(f"#define {macro}_RANGE_BYTES {c_u32(mem.size)}")
    lines.append("")

    lines.append("/* step_ip/s_axi identity and global offsets. */")
    lines.append(f"#define V3_STEP_IP_ID_VALUE {c_u32(value_for(find_register(step_global, 'ID'), 'step_ip'))}")
    lines.append(
        f"#define V3_STEP_IP_VERSION_VALUE {c_u32(value_for(find_register(step_global, 'VERSION'), 'step_ip'))}"
    )
    emit_reg_offsets(lines, "V3_STEP_IP", step_global)
    emit_reg_addrs(lines, "V3_HW_STEP_IP_BASE_ADDR", "V3_STEP_IP", step_global)
    lines.append("")

    lines.append("/* step_ip per-axis register offsets. */")
    lines.append(f"#define V3_STEP_IP_AXIS_BASE_OFFSET {c_u32(step_axis.base)}")
    lines.append(f"#define V3_STEP_IP_AXIS_STRIDE_BYTES {c_u32(step_axis.stride)}")
    emit_reg_offsets(lines, "V3_STEP_IP_AXIS", step_axis.registers)
    lines.append(
        "#define V3_STEP_IP_AXIS_REG_ABS_OFFSET(axis, reg_offset) "
        "(V3_STEP_IP_AXIS_BASE_OFFSET + ((uint32_t)(axis) * V3_STEP_IP_AXIS_STRIDE_BYTES) + (uint32_t)(reg_offset))"
    )
    lines.append(
        "#define V3_STEP_IP_AXIS_REG_ADDR(axis, reg_offset) "
        "(V3_HW_STEP_IP_BASE_ADDR + V3_STEP_IP_AXIS_REG_ABS_OFFSET((axis), (reg_offset)))"
    )
    lines.append("")

    lines.append("/* step_ip per-axis debug counter offsets. */")
    lines.append(f"#define V3_STEP_IP_AXIS_DEBUG_BASE_OFFSET {c_u32(step_debug.base)}")
    lines.append(f"#define V3_STEP_IP_AXIS_DEBUG_STRIDE_BYTES {c_u32(step_debug.stride)}")
    emit_reg_offsets(lines, "V3_STEP_IP_AXIS_DEBUG", step_debug.registers)
    lines.append(
        "#define V3_STEP_IP_AXIS_DEBUG_REG_ABS_OFFSET(axis, reg_offset) "
        "(V3_STEP_IP_AXIS_DEBUG_BASE_OFFSET + ((uint32_t)(axis) * V3_STEP_IP_AXIS_DEBUG_STRIDE_BYTES) + (uint32_t)(reg_offset))"
    )
    lines.append(
        "#define V3_STEP_IP_AXIS_DEBUG_REG_ADDR(axis, reg_offset) "
        "(V3_HW_STEP_IP_BASE_ADDR + V3_STEP_IP_AXIS_DEBUG_REG_ABS_OFFSET((axis), (reg_offset)))"
    )
    lines.append("")

    lines.append("/* step_ip per-axis live telemetry offsets. */")
    lines.append(f"#define V3_STEP_IP_AXIS_LIVE_BASE_OFFSET {c_u32(step_live.base)}")
    lines.append(f"#define V3_STEP_IP_AXIS_LIVE_STRIDE_BYTES {c_u32(step_live.stride)}")
    emit_reg_offsets(lines, "V3_STEP_IP_AXIS_LIVE", step_live.registers)
    lines.append(
        "#define V3_STEP_IP_AXIS_LIVE_REG_ABS_OFFSET(axis, reg_offset) "
        "(V3_STEP_IP_AXIS_LIVE_BASE_OFFSET + ((uint32_t)(axis) * V3_STEP_IP_AXIS_LIVE_STRIDE_BYTES) + (uint32_t)(reg_offset))"
    )
    lines.append(
        "#define V3_STEP_IP_AXIS_LIVE_REG_ADDR(axis, reg_offset) "
        "(V3_HW_STEP_IP_BASE_ADDR + V3_STEP_IP_AXIS_LIVE_REG_ABS_OFFSET((axis), (reg_offset)))"
    )
    lines.append("")

    lines.append("/* pl_estop/S_AXI register offsets. */")
    lines.append(
        f"#define V3_PL_ESTOP_MAGIC_VALUE {c_u32(value_for(find_register(estop_regs, 'MAGIC'), 'pl_estop'))}"
    )
    lines.append(
        f"#define V3_PL_ESTOP_VERSION_VALUE {c_u32(value_for(find_register(estop_regs, 'VERSION'), 'pl_estop'))}"
    )
    emit_reg_offsets(lines, "V3_PL_ESTOP", estop_regs)
    emit_reg_addrs(lines, "V3_HW_PL_ESTOP_BASE_ADDR", "V3_PL_ESTOP", estop_regs)
    lines.append("")

    lines.append("/* z20_v15_io_owner/S_AXI register offsets. */")
    lines.append(f"#define V3_IO_OWNER_MAGIC_VALUE {c_u32(value_for(find_register(io_regs, 'MAGIC'), 'io_owner'))}")
    lines.append(
        f"#define V3_IO_OWNER_VERSION_VALUE {c_u32(value_for(find_register(io_regs, 'VERSION'), 'io_owner'))}"
    )
    lines.append(
        f"#define V3_IO_OWNER_BUILD_ID_VALUE {c_u32(value_for(find_register(io_regs, 'BUILD_ID'), 'io_owner'))}"
    )
    emit_reg_offsets(lines, "V3_IO_OWNER", io_regs)
    emit_reg_addrs(lines, "V3_HW_IO_OWNER_BASE_ADDR", "V3_IO_OWNER", io_regs)
    lines.append("")

    lines.append("#endif /* V3_HARDWARE_ABI_H */")
    lines.append("")
    return "\n".join(lines)


def validate_doc_vs_xsa(label: str, doc_base: int, doc_range: int, mem: MemRange) -> None:
    if doc_base != mem.base:
        fail(f"{label} documented base 0x{doc_base:08X} != XSA base 0x{mem.base:08X}")
    if doc_range != mem.size:
        fail(f"{label} documented range 0x{doc_range:08X} != XSA range 0x{mem.size:08X}")


def build_header(project_dir: Path, xsa_path: Path) -> tuple[str, dict[str, str]]:
    ranges = parse_xsa_memranges(xsa_path)
    blocks = {
        "step_ip": expected_memrange(ranges, "step_ip", "s_axi"),
        "pl_estop": expected_memrange(ranges, "pl_estop", "S_AXI"),
        "io_owner": expected_memrange(ranges, "z20_v15_io_owner", "S_AXI"),
    }

    step_map = project_dir / "ip_repo/axi_stepdir_enc_v2/step_ip_register_map.md"
    estop_map = project_dir / "board_inputs/pl_estop_register_map.md"
    io_map = project_dir / "docs/io_owner_register_map.md"
    step_text = read_text(step_map)
    estop_text = read_text(estop_map)
    io_text = read_text(io_map)

    validate_doc_vs_xsa("step_ip", parse_doc_base(step_text, "Current Z20 v1.5 AXI base:"), parse_doc_range(step_text), blocks["step_ip"])
    validate_doc_vs_xsa("pl_estop", parse_doc_base(estop_text, "Base address:"), parse_doc_range(estop_text), blocks["pl_estop"])
    validate_doc_vs_xsa("z20_v15_io_owner", parse_doc_base(io_text, "Base address:"), parse_doc_range(io_text), blocks["io_owner"])

    step_global = parse_register_table(step_text, "## Global Registers")
    step_axis = parse_axis_section(
        step_text,
        "## Per-Axis Registers",
        r"Per-axis base:\s*`?0x([0-9A-Fa-f_]+)\s*\+\s*axis\s*\*\s*0x([0-9A-Fa-f_]+)",
    )
    step_debug = parse_axis_section(
        step_text,
        "## Axis Debug Counters",
        r"Base:\s*`?0x([0-9A-Fa-f_]+)\s*\+\s*axis\s*\*\s*0x([0-9A-Fa-f_]+)",
    )
    step_live = parse_axis_section(
        step_text,
        "## Axis Live Telemetry",
        r"Base:\s*`?0x([0-9A-Fa-f_]+)\s*\+\s*axis\s*\*\s*0x([0-9A-Fa-f_]+)",
    )
    estop_regs = parse_register_table(estop_text, "## Register Table")
    io_regs = parse_register_table(io_text, "## Register Table")

    required_step = ("ID", "VERSION", "CLK_FREQ", "N_AXES", "GLOBAL_APPLY")
    required_axis = ("CONTROL", "DELTA_STEPS", "STEP_WIDTH", "STEP_SPACE", "ENC_COUNT", "STATUS")
    required_estop = ("MAGIC", "VERSION", "STATUS", "CONTROL")
    required_io = ("MAGIC", "VERSION", "DI", "FR_DI", "MISC", "DO", "AXIS_ENA", "TOUCH_CTRL", "PWM_CTRL", "BUILD_ID")
    for name in required_step:
        find_register(step_global, name)
    for name in required_axis:
        find_register(step_axis.registers, name)
    for name in required_estop:
        find_register(estop_regs, name)
    for name in required_io:
        find_register(io_regs, name)

    header = generate_header(
        project_dir,
        xsa_path,
        blocks,
        step_global,
        step_axis,
        step_debug,
        step_live,
        estop_regs,
        io_regs,
    )
    facts = {
        "step_ip_base": f"0x{blocks['step_ip'].base:08X}",
        "step_ip_range": f"0x{blocks['step_ip'].size:08X}",
        "pl_estop_base": f"0x{blocks['pl_estop'].base:08X}",
        "pl_estop_range": f"0x{blocks['pl_estop'].size:08X}",
        "io_owner_base": f"0x{blocks['io_owner'].base:08X}",
        "io_owner_range": f"0x{blocks['io_owner'].size:08X}",
        "step_ip_global_registers": str(len(step_global)),
        "step_ip_axis_registers": str(len(step_axis.registers)),
        "step_ip_debug_registers": str(len(step_debug.registers)),
        "step_ip_live_registers": str(len(step_live.registers)),
        "pl_estop_registers": str(len(estop_regs)),
        "io_owner_registers": str(len(io_regs)),
    }
    return header, facts


def write_or_check(out_path: Path, content: str, check: bool) -> None:
    if check:
        if not out_path.exists():
            fail(f"missing generated header: {out_path}")
        current = out_path.read_text(encoding="utf-8")
        if current != content:
            diff = "\n".join(
                difflib.unified_diff(
                    current.splitlines(),
                    content.splitlines(),
                    fromfile=str(out_path),
                    tofile="generated",
                    lineterm="",
                )
            )
            sys.stderr.write(diff + "\n")
            fail("generated header is stale")
        return

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(content, encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-dir", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--xsa", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--check", action="store_true", help="fail if the existing header is stale")
    args = parser.parse_args()

    project_dir = args.project_dir.resolve()
    xsa_path = args.xsa.resolve() if args.xsa else project_dir / "board_inputs/system.xsa"
    out_path = args.out.resolve() if args.out else project_dir / "board_inputs/v3_hardware_abi.h"
    header, facts = build_header(project_dir, xsa_path)
    write_or_check(out_path, header, args.check)

    output_rel = out_path.relative_to(project_dir).as_posix()
    xsa_rel = xsa_path.relative_to(project_dir).as_posix()
    print("hardware_abi_header=ok")
    print(f"mode={'check' if args.check else 'write'}")
    print(f"output={output_rel}")
    print(f"xsa={xsa_rel}")
    print("blocks=3")
    for key, value in facts.items():
        print(f"{key}={value}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
