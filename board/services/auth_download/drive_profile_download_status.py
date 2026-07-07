from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Any, Dict, Optional
from drive_profile_download_core import now_utc

STALE_EPOCH_MAX_AGE_NS = 0


def read_request(path: Optional[Path] = None) -> Dict[str, Any]:
    request_path = path
    if request_path is None:
        return {}
    try:
        data = json.loads(request_path.read_text(encoding="utf-8-sig"))
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}

def request_status_epoch(request: Dict[str, Any]) -> Optional[int]:
    _ = request
    return None

def current_status_epoch() -> Optional[int]:
    return None

def stale_epoch_result(request_epoch: Optional[int], snapshot_epoch: Optional[int]) -> Optional[Dict[str, Any]]:
    _ = request_epoch
    _ = snapshot_epoch
    return None
