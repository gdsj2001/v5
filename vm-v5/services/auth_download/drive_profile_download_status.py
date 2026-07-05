from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Any, Dict, Optional
from drive_profile_download_core import now_utc

STALE_EPOCH_MAX_AGE_NS = 2_000_000_000


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
    try:
        value = int(request.get("status_epoch"))
        return value if value > 0 else None
    except Exception:
        return None

def current_status_epoch() -> Optional[int]:
    return time.monotonic_ns()

def stale_epoch_result(request_epoch: Optional[int], snapshot_epoch: Optional[int]) -> Optional[Dict[str, Any]]:
    if request_epoch is None:
        return None
    if snapshot_epoch is None:
        return {
            "ok": False,
            "code": "SERVER_DOWNLOAD_STATUS_EPOCH_UNAVAILABLE",
            "schema": "re-v5-drive-profile-download-v1",
            "generated_at": now_utc(),
            "message_cn": "服务器下载状态帧不可用，未下载配置",
            "request_status_epoch": request_epoch,
            "download_executed": False,
            "profile_write_executed": False,
        }
    age_ns = snapshot_epoch - request_epoch
    if age_ns <= STALE_EPOCH_MAX_AGE_NS:
        return None
    return {
        "ok": False,
        "code": "SERVER_DOWNLOAD_STALE_EPOCH",
        "schema": "re-v5-drive-profile-download-v1",
        "generated_at": now_utc(),
        "message_cn": "服务器下载请求状态已过期，未下载配置",
        "request_status_epoch": request_epoch,
        "current_status_epoch": snapshot_epoch,
        "stale_age_ns": age_ns,
        "stale_max_age_ns": STALE_EPOCH_MAX_AGE_NS,
        "download_executed": False,
        "profile_write_executed": False,
    }
