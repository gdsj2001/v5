#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import time
from pathlib import Path
from typing import Any, Dict, List

RUNTIME_PROFILE_ROOT = Path("/opt/8ax/drive-profiles")
DEFAULT_OUT = Path("/run/8ax_v5_drive/drive_profile_resident_snapshot.json")


def now_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_map(path: Path, scope: str) -> Dict[str, Any]:
    if not path.is_file():
        return {"ok": False, "scope": scope, "path": str(path), "code": "PROFILE_MAP_CACHE_MISSING", "profiles": []}
    raw = path.read_bytes()
    try:
        payload = json.loads(raw.decode("utf-8"))
    except Exception as exc:
        return {
            "ok": False,
            "scope": scope,
            "path": str(path),
            "sha256": sha256_bytes(raw),
            "code": "PROFILE_MAP_CACHE_INVALID_JSON",
            "error": "%s: %s" % (type(exc).__name__, exc),
            "profiles": [],
        }
    schema = str(payload.get("schema") or payload.get("schema_version") or "") if isinstance(payload, dict) else ""
    if schema != "v5-driver-profile-map-v1":
        return {
            "ok": False,
            "scope": scope,
            "path": str(path),
            "sha256": sha256_bytes(raw),
            "schema": schema,
            "code": "PROFILE_MAP_SCHEMA_INVALID",
            "profiles": [],
        }
    profiles = payload.get("profiles", []) if isinstance(payload, dict) else []
    if not isinstance(profiles, list):
        profiles = []
    compact_profiles: List[Dict[str, Any]] = []
    for item in profiles:
        if not isinstance(item, dict):
            continue
        copied = dict(item)
        copied["map_source"] = scope
        copied["profile_map_path"] = str(path)
        copied["profile_map_sha256"] = sha256_bytes(raw)
        compact_profiles.append(copied)
    return {
        "ok": True,
        "scope": scope,
        "path": str(path),
        "sha256": sha256_bytes(raw),
        "map_version": payload.get("map_version") if isinstance(payload, dict) else "",
        "schema": payload.get("schema") if isinstance(payload, dict) else "",
        "profile_count": len(compact_profiles),
        "profiles": compact_profiles,
    }


def build_snapshot(profile_root: Path) -> Dict[str, Any]:
    maps = [
        read_map(profile_root / "private" / "driver_profile_map.json", "private"),
        read_map(profile_root / "public" / "driver_profile_map.json", "public"),
    ]
    profiles: List[Dict[str, Any]] = []
    for item in maps:
        if item.get("ok"):
            profiles.extend(item.get("profiles", []))
    return {
        "schema": "v5.drive_profile.resident_snapshot.v1",
        "generated_at": now_utc(),
        "profile_root": str(profile_root),
        "maps": [{k: v for k, v in item.items() if k != "profiles"} for item in maps],
        "profiles": profiles,
        "profile_count": len(profiles),
        "map_file_count": sum(1 for item in maps if item.get("ok")),
        "loaded": bool(profiles),
    }


def atomic_write_json(path: Path, payload: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    tmp.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build v5 in-memory drive profile resident snapshot")
    parser.add_argument("--project-root", default="/opt/8ax/v5", help="accepted for init-script symmetry; runtime profile cache remains /opt/8ax/drive-profiles")
    parser.add_argument("--profile-root", default=str(RUNTIME_PROFILE_ROOT))
    parser.add_argument("--out", default=str(DEFAULT_OUT))
    args = parser.parse_args()
    snapshot = build_snapshot(Path(args.profile_root))
    atomic_write_json(Path(args.out), snapshot)
    print(json.dumps({"out": args.out, "map_file_count": snapshot["map_file_count"], "profile_count": snapshot["profile_count"], "loaded": snapshot["loaded"]}, ensure_ascii=False))
    return 0 if snapshot.get("loaded") else 1


if __name__ == "__main__":
    raise SystemExit(main())
