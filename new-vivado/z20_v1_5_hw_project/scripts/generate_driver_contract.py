#!/usr/bin/env python3
"""Generate Linux/LinuxCNC driver contract files from XSA and register maps."""

from __future__ import annotations

import argparse
import difflib
import re
import sys
import zipfile
from pathlib import Path
from xml.etree import ElementTree

import generate_hardware_abi_header as abi


DEFAULT_HEADER = "board_inputs/z20_v1_5_hardware_abi.h"
DEFAULT_DTSI = "board_inputs/z20_v1_5_axi_lite.dtsi"


def fail(message: str) -> None:
    raise SystemExit(f"ERROR: {message}")


def read_hwh_root(xsa_path: Path) -> ElementTree.Element:
    if not xsa_path.exists():
        fail(f"missing XSA: {xsa_path}")
    if not zipfile.is_zipfile(xsa_path):
        fail(f"XSA is not a zip container: {xsa_path}")
    with zipfile.ZipFile(xsa_path) as archive:
        hwh_names = [name for name in archive.namelist() if name.lower().endswith(".hwh")]
        if not hwh_names:
            fail("XSA does not contain a .hwh file")
        hwh_name = "system.hwh" if "system.hwh" in hwh_names else sorted(hwh_names)[0]
        return ElementTree.fromstring(archive.read(hwh_name))


def parse_pl_estop_irq_bit(xsa_path: Path) -> int:
    root = read_hwh_root(xsa_path)
    signame = None
    for elem in root.iter():
        if elem.tag.split("}")[-1] != "PORT":
            continue
        if elem.attrib.get("NAME") == "estop_irq" and elem.attrib.get("SIGIS") == "INTERRUPT":
            signame = elem.attrib.get("SIGNAME")
            break
    if not signame:
        fail("XSA missing pl_estop estop_irq interrupt port")

    concat_bit = None
    for elem in root.iter():
        if elem.tag.split("}")[-1] != "PORT":
            continue
        if elem.attrib.get("SIGNAME") != signame:
            continue
        match = re.fullmatch(r"In([0-9]+)", elem.attrib.get("NAME", ""))
        if match:
            concat_bit = int(match.group(1))
            break
    if concat_bit is None:
        fail(f"XSA missing xlconcat input for pl_estop interrupt net {signame}")

    for elem in root.iter():
        if elem.tag.split("}")[-1] != "PORT" or elem.attrib.get("NAME") != "IRQ_F2P":
            continue
        left = int(elem.attrib.get("LEFT", "0"))
        right = int(elem.attrib.get("RIGHT", "0"))
        if not (min(left, right) <= concat_bit <= max(left, right)):
            fail(f"pl_estop interrupt bit {concat_bit} is outside IRQ_F2P[{left}:{right}]")
        return concat_bit
    fail("XSA missing processing_system7_0 IRQ_F2P port")
    return -1


def gic_spi_cell_for_f2p_bit(bit: int) -> int:
    # Zynq-7000 IRQ_F2P[0] is GIC interrupt ID 61. DTS stores SPI ID minus 32.
    return 61 + bit - 32


def render_driver_header(base_header: str) -> str:
    header = base_header.replace(
        "Auto-generated hardware ABI header for Z20 v1.5.",
        "Auto-generated Linux/LinuxCNC driver contract header for Z20 v1.5.",
    )
    header = header.replace(
        "Generator: new-vivado/z20_v1_5_hw_project/scripts/generate_hardware_abi_header.py",
        "Generator: new-vivado/z20_v1_5_hw_project/scripts/generate_driver_contract.py",
    )
    header = header.replace("V3_HARDWARE_ABI_H", "Z20_V1_5_HARDWARE_ABI_H")
    return header


def hex32(value: int) -> str:
    return f"0x{value:08x}"


def parse_fact_hex(facts: dict[str, str], key: str) -> int:
    if key not in facts:
        fail(f"missing generator fact: {key}")
    return int(facts[key], 16)


def render_dtsi(facts: dict[str, str], irq_bit: int) -> str:
    step_base = parse_fact_hex(facts, "step_ip_base")
    step_range = parse_fact_hex(facts, "step_ip_range")
    estop_base = parse_fact_hex(facts, "pl_estop_base")
    estop_range = parse_fact_hex(facts, "pl_estop_range")
    io_base = parse_fact_hex(facts, "io_owner_base")
    io_range = parse_fact_hex(facts, "io_owner_range")
    irq_cell = gic_spi_cell_for_f2p_bit(irq_bit)
    gic_id = irq_cell + 32
    return f"""/*
 * Auto-generated AXI-Lite device-tree fragment for Z20 v1.5.
 * Generator: new-vivado/z20_v1_5_hw_project/scripts/generate_driver_contract.py
 * Source XSA: new-vivado/z20_v1_5_hw_project/board_inputs/system.xsa
 *
 * Intended target: PetaLinux system-user.dtsi with an existing &amba_pl node.
 */

&amba_pl {{
    z20_step_ip: step-ip@{step_base:08x} {{
        compatible = "re,z20-v1.5-step-ip", "re,v3-step-ip";
        reg = <{hex32(step_base)} {hex32(step_range)}>;
        status = "okay";
    }};

    z20_pl_estop: pl-estop@{estop_base:08x} {{
        compatible = "re,z20-v1.5-pl-estop", "re,pl-estop";
        reg = <{hex32(estop_base)} {hex32(estop_range)}>;
        interrupt-parent = <&intc>;
        interrupts = <0 {irq_cell} 4>; /* IRQ_F2P[{irq_bit}], GIC SPI {gic_id}, level-high */
        status = "okay";
    }};

    z20_io_owner: io-owner@{io_base:08x} {{
        compatible = "re,z20-v1.5-io-owner", "re,z20-io-owner";
        reg = <{hex32(io_base)} {hex32(io_range)}>;
        status = "okay";
    }};
}};
"""


def write_or_check(path: Path, content: str, check: bool) -> None:
    if check:
        if not path.exists():
            fail(f"missing generated file: {path}")
        current = path.read_text(encoding="utf-8")
        if current != content:
            diff = "\n".join(
                difflib.unified_diff(
                    current.splitlines(),
                    content.splitlines(),
                    fromfile=str(path),
                    tofile="generated",
                    lineterm="",
                )
            )
            sys.stderr.write(diff + "\n")
            fail(f"generated file is stale: {path}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-dir", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--xsa", type=Path)
    parser.add_argument("--header-out", type=Path)
    parser.add_argument("--dtsi-out", type=Path)
    parser.add_argument("--check", action="store_true", help="fail if generated files are stale")
    args = parser.parse_args()

    project_dir = args.project_dir.resolve()
    xsa_path = args.xsa.resolve() if args.xsa else project_dir / "board_inputs/system.xsa"
    header_out = args.header_out.resolve() if args.header_out else project_dir / DEFAULT_HEADER
    dtsi_out = args.dtsi_out.resolve() if args.dtsi_out else project_dir / DEFAULT_DTSI

    base_header, facts = abi.build_header(project_dir, xsa_path)
    irq_bit = parse_pl_estop_irq_bit(xsa_path)
    header = render_driver_header(base_header)
    dtsi = render_dtsi(facts, irq_bit)
    write_or_check(header_out, header, args.check)
    write_or_check(dtsi_out, dtsi, args.check)

    print("driver_contract=ok")
    print(f"mode={'check' if args.check else 'write'}")
    print(f"header={header_out.relative_to(project_dir).as_posix()}")
    print(f"dtsi={dtsi_out.relative_to(project_dir).as_posix()}")
    print(f"xsa={xsa_path.relative_to(project_dir).as_posix()}")
    print("blocks=3")
    print(f"pl_estop_irq_f2p_bit={irq_bit}")
    print(f"pl_estop_irq_gic_spi_cell={gic_spi_cell_for_f2p_bit(irq_bit)}")
    for key, value in facts.items():
        print(f"{key}={value}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
