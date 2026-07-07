from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any, Iterable, List
from drive_profile_download_errors import DownloadError

def normalize_base_url(value: Any) -> str:
    text = str(value or "").strip()
    if not text or not text.lower().startswith(("http://", "https://")):
        return ""
    return text.rstrip("/")

def split_urls(value: str) -> List[str]:
    out: List[str] = []
    for item in str(value or "").replace(";", ",").replace("\n", ",").split(","):
        url = normalize_base_url(item)
        if url:
            out.append(url)
    return out

def append_urls(out: List[str], value: Any) -> None:
    if isinstance(value, str):
        out.extend(split_urls(value))
    elif isinstance(value, dict):
        for key in ("base_url", "url", "primary", "primary_url"):
            append_urls(out, value.get(key, ""))
        for key in ("backup_urls", "backups"):
            append_urls(out, value.get(key, []))
    elif isinstance(value, Iterable):
        for item in value:
            append_urls(out, item)

def unique_urls(urls: Iterable[str]) -> List[str]:
    seen = set()
    result: List[str] = []
    for raw in urls:
        url = normalize_base_url(raw)
        if url and url not in seen:
            seen.add(url)
            result.append(url)
    return result

def load_endpoint_config(path: str) -> Dict[str, Any]:
    try:
        return json.loads(Path(path).read_text(encoding="utf-8-sig"))
    except Exception as exc:
        raise DownloadError("VPS endpoint 配置读取失败: %s" % exc)

def resolve_api_base_urls(args: argparse.Namespace) -> List[str]:
    urls: List[str] = []
    urls.extend(split_urls(os.environ.get("RE_V5_VPS_API_BASE_URLS", "")))
    urls.extend(split_urls(args.server_url))
    if not urls:
        cfg = load_endpoint_config(args.vps_endpoints_config)
        for key in ("api_base_urls", "api_urls", "server_urls"):
            append_urls(urls, cfg.get(key, []))
        append_urls(urls, cfg.get("api", {}))
        append_urls(urls, cfg.get("primary_api_url", ""))
        append_urls(urls, cfg.get("backup_api_urls", []))
    result = unique_urls(urls)
    if not result:
        raise DownloadError("VPS endpoint 配置里没有 api_base_urls")
    return result

def resolve_static_base_urls(args: argparse.Namespace) -> List[str]:
    urls: List[str] = []
    urls.extend(split_urls(os.environ.get("RE_V5_DRIVE_PROFILE_STATIC_URLS", "")))
    urls.extend(split_urls(args.static_url))
    if not urls:
        try:
            cfg = load_endpoint_config(args.vps_endpoints_config)
            for key in ("drive_profile_static_base_urls", "static_base_urls", "static_urls"):
                append_urls(urls, cfg.get(key, []))
        except Exception:
            pass
    return unique_urls(urls)
