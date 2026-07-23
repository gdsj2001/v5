#!/usr/bin/env python3
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BUS_HAL = ROOT / "board/linuxcnc/hal/v5_bus_1ms.hal"
BUS_INI = ROOT / "board/linuxcnc/ini/v5_bus.ini"
BUS_XML = ROOT / "board/linuxcnc/hal/ethercat-conf-1ms.xml"
PULSE_HAL = ROOT / "board/linuxcnc/hal/v5_pulse.hal"
PULSE_INI = ROOT / "board/linuxcnc/ini/v5_pulse.ini"
LINUXCNC_LAUNCHER = ROOT / "linuxcnc/scripts/linuxcnc.in"


def validate(bus_hal: str, bus_ini: str, bus_xml: str, launcher: str) -> None:
    retired = (r"^\s*loadrt\s+\[EMCMOT\]TPMOD\s*$",
               r"^\s*loadrt\s+\[EMCMOT\]HOMEMOD\s*$")
    for pattern in retired:
        assert re.search(pattern, bus_hal, flags=re.MULTILINE) is None
    assert re.search(r"^TPMOD\s*=\s*tpmod\s*$", bus_ini, flags=re.MULTILINE)
    assert re.search(r"^HOMEMOD\s*=\s*v5_bus_homecomp\s*$", bus_ini,
                     flags=re.MULTILINE)
    assert launcher.count('eval $HALCMD loadrt "$TPMOD"') == 1
    assert launcher.count('eval $HALCMD loadrt "$HOMEMOD"') == 1
    assert "TPMOD=${retval:-tpmod}" in launcher
    assert "GetFromIniQuiet HOMEMOD EMCMOT" in launcher
    assert re.search(r"^SERVO_PERIOD\s*=\s*1000000\s*$", bus_ini, re.MULTILINE)
    assert re.search(r"^ARC_BLEND_GAP_CYCLES\s*=\s*8\s*$", bus_ini, re.MULTILINE)
    assert "/opt/8ax/v5/linuxcnc/hal/v5_bus_1ms.hal" in bus_ini
    assert "/opt/8ax/v5/linuxcnc/hal/ethercat-conf-1ms.xml" in bus_hal
    assert '<master idx="0" appTimePeriod="1000000" refClockSyncCycles="10">' in bus_xml
    assert bus_xml.count('sync0Cycle="*1" sync0Shift="0"') == 5
    assert "v5_bus_2ms.hal" not in bus_hal + bus_ini + bus_xml
    assert "ethercat-conf-2ms.xml" not in bus_hal + bus_ini + bus_xml


hal_text = BUS_HAL.read_text(encoding="utf-8")
ini_text = BUS_INI.read_text(encoding="utf-8")
xml_text = BUS_XML.read_text(encoding="utf-8")
pulse_text = PULSE_HAL.read_text(encoding="utf-8")
pulse_ini_text = PULSE_INI.read_text(encoding="utf-8")
launcher_text = LINUXCNC_LAUNCHER.read_text(encoding="utf-8")
validate(hal_text, ini_text, xml_text, launcher_text)

# Pulse remains an independently supported owner and is not rewritten by this
# BUS-only retirement slice.
assert PULSE_HAL.exists()
assert re.search(r"^SERVO_PERIOD\s*=\s*4500000\s*$", pulse_ini_text, re.MULTILINE)
for joint in range(6):
    assert re.search(
        rf"^\s*setp\s+zynq_stepgen_hw\.{joint}\.scale\s+\[JOINT_{joint}\]SCALE\s*$",
        pulse_text,
        flags=re.MULTILINE,
    ), f"Pulse stepgen {joint} scale is not sourced from its runtime JOINT"
assert re.search(
    r"^\s*setp\s+zynq_stepgen_hw\.\d+\.scale\s+[-+]?\d",
    pulse_text,
    flags=re.MULTILINE,
) is None, "Pulse stepgen scale must not retain a numeric fallback"

for retired_line in ("loadrt [EMCMOT]TPMOD", "loadrt [EMCMOT]HOMEMOD"):
    try:
        validate(retired_line + "\n" + hal_text, ini_text, xml_text, launcher_text)
    except AssertionError:
        pass
    else:
        raise AssertionError("retired BUS module load was reintroduced: " + retired_line)

print("V5_BUS_MODULE_OWNER_SMOKE_OK")
