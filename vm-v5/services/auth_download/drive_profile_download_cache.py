from __future__ import annotations

import shutil
import time
from pathlib import Path
from typing import Any, Dict, List

RUN_DIR = Path("/run/8ax_v5_auth_download")


def remove_private_cache(private_dir: Path) -> List[str]:
    removed: List[str] = []
    if not private_dir.exists():
        return removed
    for path in sorted(private_dir.rglob("*"), reverse=True):
        try:
            if path.is_file() or path.is_symlink():
                removed.append(str(path))
                path.unlink()
            elif path.is_dir():
                path.rmdir()
        except Exception:
            pass
    try:
        private_dir.rmdir()
    except Exception:
        pass
    return removed

def remove_legacy_effective_cache(root: Path) -> List[str]:
    effective_dir = root / "effective"
    removed: List[str] = []
    if not effective_dir.exists():
        return removed
    for path in sorted(effective_dir.rglob("*"), reverse=True):
        try:
            if path.is_file() or path.is_symlink():
                removed.append(str(path))
                path.unlink()
            elif path.is_dir():
                path.rmdir()
        except Exception:
            pass
    try:
        effective_dir.rmdir()
        removed.append(str(effective_dir))
    except Exception:
        pass
    return removed

def invalidate_private_active_mapping() -> Dict[str, Any]:
    active_path = RUN_DIR / "active_drive_command_map.json"
    if not active_path.exists():
        return {"path": str(active_path), "existed": False, "invalidated": False}
    try:
        text = active_path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        text = ""
    if "private" not in text.lower():
        return {"path": str(active_path), "existed": True, "invalidated": False, "reason": "not_private"}
    backup = active_path.with_name("%s.invalidated_%d" % (active_path.name, int(time.time())))
    try:
        shutil.move(str(active_path), str(backup))
        return {"path": str(active_path), "existed": True, "invalidated": True, "backup": str(backup)}
    except Exception as exc:
        return {"path": str(active_path), "existed": True, "invalidated": False, "error": str(exc)}
