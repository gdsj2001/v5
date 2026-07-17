#!/usr/bin/env python3
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BUS_HAL = ROOT / "board/linuxcnc/hal/v5_bus_2ms.hal"
BUS_INI = ROOT / "board/linuxcnc/ini/v5_bus.ini"
PULSE_HAL = ROOT / "board/linuxcnc/hal/v5_pulse.hal"
LINUXCNC_LAUNCHER = ROOT / "linuxcnc/scripts/linuxcnc.in"


def validate(bus_hal: str, bus_ini: str, launcher: str) -> None:
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


hal_text = BUS_HAL.read_text(encoding="utf-8")
ini_text = BUS_INI.read_text(encoding="utf-8")
launcher_text = LINUXCNC_LAUNCHER.read_text(encoding="utf-8")
validate(hal_text, ini_text, launcher_text)

# Pulse remains an independently supported owner and is not rewritten by this
# BUS-only retirement slice.
assert PULSE_HAL.exists()

for retired_line in ("loadrt [EMCMOT]TPMOD", "loadrt [EMCMOT]HOMEMOD"):
    try:
        validate(retired_line + "\n" + hal_text, ini_text, launcher_text)
    except AssertionError:
        pass
    else:
        raise AssertionError("retired BUS module load was reintroduced: " + retired_line)

print("V5_BUS_MODULE_OWNER_SMOKE_OK")
