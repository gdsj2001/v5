#!/usr/bin/env python3
import json
import os
import sys
from pathlib import Path

CAL_PATH = Path("/opt/8ax/safe_ui/re_touch_calibration.json")
POINTERCAL_PATH = Path("/etc/pointercal")
SCALE = 65536
CAL_MODE = "raw-evdev-cal-v2"
CAL_PROFILE = "screen-fit-topleft-1024x600-v1"
CAL_ANCHOR = "topleft-v1"
POINTERCAL_REQUIRED = os.environ.get("V5_POINTERCAL_REQUIRED", "0") == "1"


def _write_text_atomic(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(f".{path.name}.tmp")
    tmp.write_text(text, encoding="utf-8")
    os.replace(tmp, path)


def _load_calibration():
    if not CAL_PATH.exists() or CAL_PATH.stat().st_size == 0:
        raise SystemExit(f"missing calibration file: {CAL_PATH}")
    data = json.loads(CAL_PATH.read_text(encoding="utf-8"))
    mode = data.get("mode")
    if mode == CAL_MODE:
        profile = data.get("profile")
        if profile != CAL_PROFILE:
            raise SystemExit(f"unsupported calibration profile: {profile!r}")
        if data.get("anchor") != CAL_ANCHOR:
            raise SystemExit(f"unsupported calibration anchor: {data.get('anchor')!r}")
    else:
        raise SystemExit(f"unsupported calibration mode: {mode!r}")
    return data


def main():
    data = _load_calibration()
    sx = float(data["sx"])
    sxy = float(data.get("sxy", 0.0))
    syx = float(data.get("syx", 0.0))
    sy = float(data["sy"])
    ox = float(data["ox"])
    oy = float(data["oy"])

    a = int(round(sx * SCALE))
    b = int(round(sxy * SCALE))
    c = int(round(ox * SCALE))
    d = int(round(syx * SCALE))
    e = int(round(sy * SCALE))
    f = int(round(oy * SCALE))
    line = f"{a} {b} {c} {d} {e} {f} {SCALE} 1024 600 0\n"
    try:
        _write_text_atomic(POINTERCAL_PATH, line)
    except PermissionError as exc:
        if POINTERCAL_REQUIRED:
            raise SystemExit(f"failed to write {POINTERCAL_PATH}: {exc}")
        print(f"WARN: skip writing {POINTERCAL_PATH}: {exc}", file=sys.stderr)
    except OSError as exc:
        if POINTERCAL_REQUIRED:
            raise SystemExit(f"failed to write {POINTERCAL_PATH}: {exc}")
        print(f"WARN: skip writing {POINTERCAL_PATH}: {exc}", file=sys.stderr)
    print(f"src={CAL_PATH}")
    print(line.strip())


if __name__ == "__main__":
    main()

