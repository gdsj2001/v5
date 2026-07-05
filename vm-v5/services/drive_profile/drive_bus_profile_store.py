#!/usr/bin/env python3
"""Drive profile map loading utilities for the boot RAM snapshot."""

from __future__ import annotations

from pathlib import Path
from typing import Any, Callable, Dict, Iterable, List, Optional


def safe_scope_detail_path(profile_root: Path, scope: str, rel: str) -> Optional[Path]:
    rel = str(rel or "").strip().replace("\\", "/")
    if not rel or rel.startswith("/") or ".." in rel.split("/"):
        return None
    root = (profile_root / scope).resolve()
    candidate = (root / rel).resolve()
    try:
        candidate.relative_to(root)
    except ValueError:
        return None
    return candidate


def load_profiles(
    profile_root: Path,
    scopes: Iterable[str],
    *,
    read_json_fn: Callable[[Path], Dict[str, Any]],
) -> List[Dict[str, Any]]:
    result: List[Dict[str, Any]] = []
    for scope in scopes:
        profile_map = profile_root / scope / "driver_profile_map.json"
        payload = read_json_fn(profile_map)
        profiles = payload.get("profiles", []) if isinstance(payload, dict) else []
        map_scope = str(payload.get("map_scope") or payload.get("map_role") or scope).strip().lower()
        if map_scope not in {"private", "public"}:
            map_scope = scope
        for item in profiles:
            if not isinstance(item, dict):
                continue
            full = dict(item)
            map_source = str(full.get("map_source") or full.get("source") or map_scope).strip().lower()
            if map_source not in {"private", "public"}:
                map_source = scope
            full["map_source"] = map_source
            full["map_scope"] = map_scope
            full["profile_map_path"] = str(profile_map)
            detail: Dict[str, Any] = {}
            detail_path = safe_scope_detail_path(profile_root, scope, str(item.get("profile_file") or ""))
            if detail_path is not None:
                detail = read_json_fn(detail_path)
            if not detail:
                detail = dict(full)
            detail.setdefault("profile_id", full.get("profile_id", ""))
            detail.setdefault("map_source", map_source)
            detail.setdefault("map_scope", map_scope)
            detail.setdefault("profile_map_path", str(profile_map))
            full["profile_detail"] = detail
            result.append(full)
    return result
