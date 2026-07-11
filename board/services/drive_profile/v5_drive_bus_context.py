from __future__ import annotations

from typing import Any, Dict


resident_snapshot_cache: Dict[str, Any] | None = None
settings_runtime_cache: Dict[str, Any] | None = None
runtime_ini_sections_cache: Dict[str, Dict[str, Dict[str, str]]] = {}
self_slave_binding_cache: Dict[str, str] | None = None
resident_preload_active = False


def reset_resident_preload_caches() -> None:
    global resident_snapshot_cache
    global settings_runtime_cache
    global self_slave_binding_cache
    resident_snapshot_cache = None
    settings_runtime_cache = None
    self_slave_binding_cache = None
    runtime_ini_sections_cache.clear()
