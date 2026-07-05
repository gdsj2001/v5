#!/usr/bin/env python3
"""Register the board PL Device DNA through the factory VPS endpoint.

The UI never displays or stores the full DNA in logs. This script reads the
live hardware DNA, posts it as the protected request header expected by the
8AX VPS API, and stores only the signed device authorization file.
"""

from __future__ import annotations
from device_auth_latch import DEFAULT_AUTH_LATCH_STATUS_PATH, write_device_auth_latch
from device_dna_register_auth import DnaRegisterError, atomic_write, atomic_write_json, device_authorization_signature_hash, dna_hash_hex, existing_authorization_status, load_json_bytes, now_utc, prepare_device_keypair, store_response_device_authorization, verify_device_authorization
from device_dna_register_hardware import read_live_dna
from device_vps_identity import extract_vps_distribution_id, identity_fields, public_identity_fields, require_registered_identity, scrub_local_dna_fields

import argparse
import base64
import json
import os
import re
import struct
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional

RUN_DIR = Path("/run/8ax_v5_auth_download")
REQUEST_STATUS_EPOCH: Optional[int] = None
STALE_EPOCH_MAX_AGE_NS = 2_000_000_000
DEFAULT_CHOSEN_DIR = "/proc/device-tree/chosen"
DEFAULT_VPS_ENDPOINTS_CONFIG = "/etc/6x-cnc/vps_endpoints.json"
DEFAULT_FACTORY_TOKEN_FILE = "/etc/6x-cnc/factory_device_register_token"
DEFAULT_REGISTER_STATUS_PATH = "/opt/8ax/drive-profiles/device_register_status.json"
DEFAULT_DEVICE_AUTH_FILE = "/etc/6x-cnc/device_authorization.json"
DEFAULT_DEVICE_AUTH_PUBLIC_KEY_FILE = "/etc/6x-cnc/device_auth_public.pem"
DEFAULT_DEVICE_PRIVATE_KEY_FILE = "/etc/6x-cnc/device_private_key.pem"
DEFAULT_DEVICE_PUBLIC_KEY_FILE = "/etc/6x-cnc/device_public_key.pem"
EXPECTED_DNA_BITS = 57
EXPECTED_MAGIC = 0x444E4130
EXPECTED_VERSION = 0x00010000
EXPECTED_STATUS = 0x00000007
DNA_HI_MASK = (1 << (EXPECTED_DNA_BITS - 32)) - 1
DNA_VALUE_RE = re.compile(r"^(?:0x)?([0-9a-fA-F]{16})$")
DEVICE_AUTH_SCHEMA = "8ax-device-authorization-v1"
DEVICE_AUTH_ENVELOPE_SCHEMA = "8ax-device-authorization-envelope-v1"
DEVICE_AUTH_SIGNATURE_ALG = "RSASSA-PKCS1-v1_5-SHA256"

def local_result(payload: Dict[str, Any]) -> Dict[str, Any]:
    cleaned = scrub_local_dna_fields(payload)
    return cleaned if isinstance(cleaned, dict) else dict(payload)

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
            "code": "DNA_REGISTER_STATUS_EPOCH_UNAVAILABLE",
            "schema": "re-v5-device-dna-register-v1",
            "generated_at": now_utc(),
            "message_cn": "设备 DNA 注册状态帧不可用，未读取硬件 DNA",
            "request_status_epoch": request_epoch,
            "dna_read_executed": False,
            "network_request_executed": False,
        }
    age_ns = snapshot_epoch - request_epoch
    if age_ns <= STALE_EPOCH_MAX_AGE_NS:
        return None
    return {
        "ok": False,
        "code": "DNA_REGISTER_STALE_EPOCH",
        "schema": "re-v5-device-dna-register-v1",
        "generated_at": now_utc(),
        "message_cn": "设备 DNA 注册请求状态已过期，未读取硬件 DNA",
        "request_status_epoch": request_epoch,
        "current_status_epoch": snapshot_epoch,
        "stale_age_ns": age_ns,
        "stale_max_age_ns": STALE_EPOCH_MAX_AGE_NS,
        "dna_read_executed": False,
        "network_request_executed": False,
        "authorization_write_executed": False,
    }

def attach_request_status_epoch(payload: Dict[str, Any]) -> Dict[str, Any]:
    if REQUEST_STATUS_EPOCH is None or "request_status_epoch" in payload:
        return payload
    updated = dict(payload)
    updated["request_status_epoch"] = REQUEST_STATUS_EPOCH
    return updated

def job_token_from_context(context: Optional[Dict[str, Any]]) -> Any:
    if isinstance(context, dict):
        return context.get("job_token")
    return None

def raise_if_cancelled(job_token: Any) -> None:
    if job_token is not None and hasattr(job_token, "raise_if_cancelled"):
        job_token.raise_if_cancelled()

def load_json(path: str) -> Dict[str, Any]:
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))

def read_text(path: str) -> str:
    try:
        return Path(path).read_text(encoding="utf-8-sig").strip()
    except Exception:
        return ""

def normalize_base_url(value: Any) -> str:
    text = str(value or "").strip()
    if not text or not re.match(r"^https?://", text, re.IGNORECASE):
        return ""
    return text.rstrip("/")

def split_urls(value: str) -> List[str]:
    out: List[str] = []
    for item in re.split(r"[\s,;]+", str(value or "")):
        url = normalize_base_url(item)
        if url:
            out.append(url)
    return out

def append_urls(out: List[str], value: Any) -> None:
    if isinstance(value, str):
        out.extend(split_urls(value))
        return
    if isinstance(value, dict):
        for key in ("base_url", "url", "primary", "primary_url"):
            append_urls(out, value.get(key, ""))
        for key in ("backup_urls", "backups"):
            append_urls(out, value.get(key, []))
        return
    if isinstance(value, Iterable):
        for item in value:
            append_urls(out, item)

def unique_urls(urls: Iterable[str]) -> List[str]:
    seen = set()
    out: List[str] = []
    for raw in urls:
        url = normalize_base_url(raw)
        if url and url not in seen:
            seen.add(url)
            out.append(url)
    return out

def resolve_api_base_urls(args: argparse.Namespace) -> List[str]:
    urls: List[str] = []
    urls.extend(split_urls(os.environ.get("RE_V5_VPS_API_BASE_URLS", "")))
    urls.extend(split_urls(os.environ.get("AX8_VPS_API_BASE_URLS", "")))
    if args.server_url:
        urls.extend(split_urls(args.server_url))
    if not urls:
        cfg = load_json(args.vps_endpoints_config)
        for key in ("api_base_urls", "api_urls", "server_urls"):
            append_urls(urls, cfg.get(key, []))
        append_urls(urls, cfg.get("api", {}))
        append_urls(urls, cfg.get("primary_api_url", ""))
        append_urls(urls, cfg.get("backup_api_urls", []))
    result = unique_urls(urls)
    if not result:
        raise DnaRegisterError("DNA_REGISTER_ENDPOINT_MISSING", "VPS endpoint 配置里没有 api_base_urls")
    return result

def http_post_json(url: str, token: str, timeout: float, payload: Dict[str, Any], dna: str) -> Dict[str, Any]:
    headers = {
        "Accept": "application/json",
        "Content-Type": "application/json; charset=utf-8",
        "User-Agent": "re-v5-device-dna-register/1",
        "X-8AX-Device-DNA": dna,
        "X-8AX-License-Anchor": dna,
    }
    if token:
        headers["Authorization"] = "Bearer " + token
    data = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers=headers, method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return load_json_bytes(resp.read(), url)

def redact_text(text: Any, dna: str) -> str:
    value = str(text or "")
    if dna:
        value = value.replace(dna, "<redacted>")
        value = value.replace(dna.upper(), "<redacted>")
        value = value.replace(dna.lower(), "<redacted>")
    return value

def redact_response(response: Dict[str, Any], dna: str) -> Dict[str, Any]:
    out: Dict[str, Any] = {}
    for key, value in response.items():
        if key in ("deviceAuthorization", "device_authorization") and isinstance(value, dict):
            out[key] = {
                "stored_locally": True,
                "schema": value.get("schema", ""),
                "signature_hash": device_authorization_signature_hash(value),
            }
        elif isinstance(value, str):
            out[key] = redact_text(value, dna)
        else:
            out[key] = value
    return out

def register_device_dna(args: argparse.Namespace, job_token: Any = None) -> Dict[str, Any]:
    raise_if_cancelled(job_token)
    dna_report = read_live_dna()
    dna = dna_report["value"]
    dna_hash = dna_hash_hex(dna)
    token = args.token or os.environ.get("AX8_FACTORY_DEVICE_REGISTER_TOKEN", "") or read_text(args.factory_token_file)
    if not token:
        raise DnaRegisterError("DNA_REGISTER_TOKEN_MISSING", "厂家 DNA 登记 token 缺失")
    raise_if_cancelled(job_token)
    server_urls = resolve_api_base_urls(args)
    key_info = prepare_device_keypair(args, create=True)
    request_payload = {
        "source": "re-v5-lvgl-settings",
        "device_id_source": dna_report.get("source", ""),
        "license_anchor_type": dna_report.get("type", "zynq7000_pl_device_dna_57"),
        "device_public_key_pem": key_info["public_key_pem"],
        "device_public_key_sha256": key_info["public_key_sha256"],
        "software_version": os.environ.get("RE_V5_SOFTWARE_VERSION", os.environ.get("AX8_SOFTWARE_VERSION", "")),
        "client_time": now_utc(),
    }
    errors: List[str] = []
    for server_url in server_urls:
        raise_if_cancelled(job_token)
        url = "%s/api/v1/factory/devices/register-dna" % server_url
        try:
            response = http_post_json(url, token, args.timeout, request_payload, dna)
            raise_if_cancelled(job_token)
            ok = bool(response.get("success", response.get("ok", False)))
            if not ok:
                errors.append("%s:%s" % (url, redact_text(response.get("message", "server returned not ok"), dna)))
                continue
            auth_status: Dict[str, Any]
            code = "DNA_REGISTER_UPLOADED_PENDING_AUTH"
            message_cn = "本机 DNA 已上传，等待工厂客户端生成授权文件"
            if isinstance(response.get("deviceAuthorization") or response.get("device_authorization"), dict):
                auth_status = store_response_device_authorization(response, args, dna, key_info)
                code = "DNA_REGISTER_OK"
                message_cn = "本机 DNA 登记完成，授权文件已验签保存"
            else:
                auth_status = {
                    "stored": False,
                    "path": args.device_auth_file,
                    "pl_dna_hash": dna_hash,
                    "device_public_key_sha256": key_info["public_key_sha256"],
                    "download_required": True,
                }
            vps_id = extract_vps_distribution_id(response) or extract_vps_distribution_id(auth_status)
            if not vps_id:
                errors.append("%s:VPS did not return valid 6-digit vpsDistributionId" % url)
                continue
            identity = identity_fields(vps_id, dna_hash)
            public_identity = public_identity_fields(vps_id)
            auth_status.update(identity)
            result = {
                "ok": True,
                "code": code,
                "schema": "re-v5-device-dna-register-v1",
                "generated_at": now_utc(),
                "message_cn": message_cn,
                "server_url": server_url,
                "server_attempted": True,
                **public_identity,
                "license_anchor": {
                    "source": dna_report.get("source", ""),
                    "type": dna_report.get("type", ""),
                    "value_stored_locally": False,
                    "hash": dna_hash,
                    "hash_short": dna_hash[:8],
                },
                "device_public_key": {
                    "path": args.device_public_key_file,
                    "sha256": key_info["public_key_sha256"],
                },
                "device_authorization": auth_status,
                "server": redact_response(response, dna),
            }
            if bool(auth_status.get("stored")):
                write_device_auth_latch(result, args.auth_latch_status_path)
            public_result = local_result(result)
            atomic_write_json(Path(args.register_status_path), public_result)
            return public_result
        except urllib.error.HTTPError as exc:
            body = ""
            try:
                body = exc.read().decode("utf-8", errors="replace")[:800]
            except Exception:
                pass
            errors.append(redact_text("%s:HTTP%s:%s" % (url, exc.code, body), dna))
        except Exception as exc:
            errors.append(redact_text("%s:%s" % (url, exc), dna))

    try:
        raise_if_cancelled(job_token)
        auth_status = existing_authorization_status(args, dna, key_info)
        identity = require_registered_identity(args.register_status_path, dna_hash)
        auth_status.update(identity)
        result = {
            "ok": True,
            "code": "DNA_REGISTER_ALREADY_VALID",
            "schema": "re-v5-device-dna-register-v1",
            "generated_at": now_utc(),
            "message_cn": "本机已有有效 DNA 登记凭证；本次 VPS 重登未完成或无需重复",
            "server_attempted": True,
            "server_errors": errors[-4:],
            **identity,
            "license_anchor": {
                "source": dna_report.get("source", ""),
                "type": dna_report.get("type", ""),
                "value_stored_locally": False,
                "hash": dna_hash,
                "hash_short": dna_hash[:8],
            },
            "device_public_key": {
                "path": args.device_public_key_file,
                "sha256": key_info["public_key_sha256"],
            },
            "device_authorization": auth_status,
        }
        write_device_auth_latch(result, args.auth_latch_status_path)
        public_result = local_result(result)
        atomic_write_json(Path(args.register_status_path), public_result)
        return public_result
    except Exception as exc:
        result = {
            "ok": False,
            "code": "DNA_REGISTER_FAILED",
            "schema": "re-v5-device-dna-register-v1",
            "generated_at": now_utc(),
            "message_cn": "本机 DNA 登记失败：请检查网络、VPS 接口、厂家 token 和本地授权文件",
            "server_attempted": True,
            "server_errors": errors[-4:],
            "local_auth_error": redact_text(exc, dna),
            "license_anchor": {
                "source": dna_report.get("source", ""),
                "type": dna_report.get("type", ""),
                "value_stored_locally": False,
                "hash": dna_hash,
                "hash_short": dna_hash[:8],
            },
        }
        public_result = local_result(result)
        atomic_write_json(Path(args.register_status_path), public_result)
        return public_result

def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Register RE v5 board PL Device DNA.")
    parser.add_argument("--vps-endpoints-config", default=os.environ.get("RE_V5_VPS_ENDPOINTS_CONFIG", os.environ.get("AX8_VPS_ENDPOINTS_CONFIG", DEFAULT_VPS_ENDPOINTS_CONFIG)))
    parser.add_argument("--server-url", default="")
    parser.add_argument("--token", default="")
    parser.add_argument("--factory-token-file", default=os.environ.get("AX8_FACTORY_DEVICE_REGISTER_TOKEN_FILE", DEFAULT_FACTORY_TOKEN_FILE))
    parser.add_argument("--register-status-path", default=os.environ.get("RE_v5_device_dna_register_STATUS_PATH", DEFAULT_REGISTER_STATUS_PATH))
    parser.add_argument("--auth-latch-status-path", default=os.environ.get("RE_V5_DEVICE_AUTH_LATCH_STATUS", DEFAULT_AUTH_LATCH_STATUS_PATH))
    parser.add_argument("--device-auth-file", default=os.environ.get("AX8_DEVICE_AUTH_FILE", DEFAULT_DEVICE_AUTH_FILE))
    parser.add_argument("--device-auth-public-key-file", default=os.environ.get("AX8_DEVICE_AUTH_PUBLIC_KEY_FILE", DEFAULT_DEVICE_AUTH_PUBLIC_KEY_FILE))
    parser.add_argument("--device-private-key-file", default=os.environ.get("AX8_DEVICE_PRIVATE_KEY_FILE", DEFAULT_DEVICE_PRIVATE_KEY_FILE))
    parser.add_argument("--device-public-key-file", default=os.environ.get("AX8_DEVICE_PUBLIC_KEY_FILE", DEFAULT_DEVICE_PUBLIC_KEY_FILE))
    parser.add_argument("--timeout", type=float, default=float(os.environ.get("RE_v5_device_dna_register_TIMEOUT", "15")))
    parser.add_argument("--request", default="")
    return parser.parse_args(argv)

def run_action(request: Dict[str, Any], context: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    global REQUEST_STATUS_EPOCH

    job_token = job_token_from_context(context)
    raise_if_cancelled(job_token)
    REQUEST_STATUS_EPOCH = request_status_epoch(request)
    stale_result = stale_epoch_result(REQUEST_STATUS_EPOCH, current_status_epoch())
    if stale_result is not None:
        result = attach_request_status_epoch(stale_result)
        RUN_DIR.mkdir(parents=True, exist_ok=True)
        atomic_write_json(RUN_DIR / "device_dna_register_result.json", result)
        return result

    args = parse_args([])
    try:
        result = register_device_dna(args, job_token=job_token)
    except DnaRegisterError as exc:
        result = {
            "ok": False,
            "code": exc.code,
            "schema": "re-v5-device-dna-register-v1",
            "generated_at": now_utc(),
            "message_cn": exc.message,
        }
    except Exception as exc:
        result = {
            "ok": False,
            "code": "DNA_REGISTER_EXCEPTION",
            "schema": "re-v5-device-dna-register-v1",
            "generated_at": now_utc(),
            "message_cn": "本机 DNA 登记异常，未隐藏错误",
            "error": repr(exc),
        }
    result = attach_request_status_epoch(result)
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    atomic_write_json(RUN_DIR / "device_dna_register_result.json", result)
    return result

def main() -> int:
    args = parse_args()
    request_path = Path(args.request) if getattr(args, "request", "") else None
    result = run_action(read_request(request_path))
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    return 0 if result.get("ok") else 2

if __name__ == "__main__":
    raise SystemExit(main())

