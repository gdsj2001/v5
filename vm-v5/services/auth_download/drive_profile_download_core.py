from __future__ import annotations

import base64
import hashlib
import json
import os
import re
import tempfile
import time
from pathlib import Path
from typing import Any, Dict
from drive_profile_download_errors import DownloadError

SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")


def now_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

def b64url_encode(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode("ascii").rstrip("=")

def canonical_json_bytes(data: Dict[str, Any]) -> bytes:
    return json.dumps(data, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")

def read_text_token(path: str) -> str:
    try:
        return Path(path).read_text(encoding="utf-8", errors="ignore").strip()
    except FileNotFoundError:
        return ""

def load_json_bytes(data: bytes, source: str) -> Dict[str, Any]:
    try:
        payload = json.loads(data.decode("utf-8-sig"))
    except Exception as exc:
        raise DownloadError("%s 不是有效 JSON: %s" % (source, exc))
    if not isinstance(payload, dict):
        raise DownloadError("%s JSON 根节点不是对象" % source)
    return payload

def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()

def parse_sha256_sidecar(data: bytes, source: str) -> str:
    try:
        text = data.decode("ascii", errors="strict").strip()
    except Exception as exc:
        raise DownloadError("%s SHA256 声明不是 ASCII: %s" % (source, exc))
    token = text.split()[0] if text.split() else ""
    if not SHA256_RE.match(token):
        raise DownloadError("%s SHA256 声明无效" % source)
    return token.upper()

def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=str(path.parent))
    try:
        with os.fdopen(fd, "wb") as fh:
            fh.write(data)
            fh.flush()
            os.fsync(fh.fileno())
        os.replace(tmp_name, str(path))
    finally:
        try:
            os.unlink(tmp_name)
        except FileNotFoundError:
            pass

def atomic_write_json(path: Path, payload: Dict[str, Any]) -> None:
    atomic_write(path, (json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8"))
