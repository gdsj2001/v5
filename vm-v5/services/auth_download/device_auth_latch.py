#!/usr/bin/env python3
"""Device authorization latch helpers.

The latch belongs to VPS/auth/download setup and diagnostics. Motion hot paths
must not read this JSON latch as a product gate; live motion authorization and
safety stay in native/LinuxCNC/HAL/EtherCAT owners.
"""

from __future__ import annotations

import json
import os
import tempfile
import time
from pathlib import Path
from types import SimpleNamespace
from typing import Any, Mapping
from device_vps_identity import extract_vps_distribution_id, require_registered_identity, scrub_local_dna_fields


DEFAULT_AUTH_LATCH_STATUS_PATH = "/run/8ax_v5_auth_download/device_auth_latch_status.json"
DEFAULT_DEVICE_AUTH_FILE = "/etc/6x-cnc/device_authorization.json"
DEFAULT_DEVICE_AUTH_PUBLIC_KEY_FILE = "/etc/6x-cnc/device_auth_public.pem"
DEFAULT_DEVICE_PRIVATE_KEY_FILE = "/etc/6x-cnc/device_private_key.pem"
DEFAULT_DEVICE_PUBLIC_KEY_FILE = "/etc/6x-cnc/device_public_key.pem"
DEFAULT_REGISTER_STATUS_PATH = "/opt/8ax/drive-profiles/device_register_status.json"
LATCH_SCHEMA = "re-v5-device-auth-latch-v1"
LATCH_VALID_CODE = "DEVICE_AUTH_LATCH_VALID"
REQUIRED_PERMISSION = "drive_profile_download"


def now_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def _normal_hash(value: Any) -> str:
    text = str(value or "").strip().lower()
    if len(text) != 64:
        return ""
    if any(ch not in "0123456789abcdef" for ch in text):
        return ""
    return text


def _read_boot_id() -> str:
    try:
        return Path("/proc/sys/kernel/random/boot_id").read_text(encoding="ascii").strip()
    except Exception:
        return ""


def _read_consistent_live_dna(sample_count: int = 3) -> dict[str, Any]:
    from device_dna_register_auth import DnaRegisterError
    from device_dna_register_hardware import read_live_dna

    count = max(1, int(sample_count))
    reports = [read_live_dna() for _ in range(count)]
    values = {str(report.get("value") or "").strip().upper() for report in reports}
    values.discard("")
    if len(values) != 1:
        raise DnaRegisterError("DEVICE_AUTH_LATCH_DNA_UNSTABLE", "live Device DNA samples were inconsistent")
    result = dict(reports[-1])
    result["sample_count"] = count
    return result


def _atomic_write_json(path: Path, payload: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=str(path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            json.dump(dict(payload), fh, ensure_ascii=False, indent=2, sort_keys=True)
            fh.write("\n")
            fh.flush()
            os.fsync(fh.fileno())
        os.replace(tmp_name, path)
    finally:
        try:
            os.unlink(tmp_name)
        except FileNotFoundError:
            pass


def build_device_auth_latch(status: Mapping[str, Any]) -> dict[str, Any]:
    license_anchor = status.get("license_anchor") if isinstance(status.get("license_anchor"), Mapping) else {}
    authorization = status.get("device_authorization") if isinstance(status.get("device_authorization"), Mapping) else {}
    anchor_hash = _normal_hash(license_anchor.get("hash") if isinstance(license_anchor, Mapping) else "")
    auth_hash = _normal_hash(authorization.get("pl_dna_hash") if isinstance(authorization, Mapping) else "")
    stored = authorization.get("stored") is True if isinstance(authorization, Mapping) else False
    permissions = authorization.get("permissions")
    safe_permissions = [str(item) for item in permissions] if isinstance(permissions, list) else []
    if (
        status.get("ok") is not True
        or not stored
        or not anchor_hash
        or anchor_hash != auth_hash
        or REQUIRED_PERMISSION not in safe_permissions
    ):
        return {
            "schema": LATCH_SCHEMA,
            "ok": False,
            "code": "DEVICE_AUTH_LATCH_INVALID",
            "state": "degraded",
            "generated_at": now_utc(),
            "message": "device authorization latch source is not verified",
            "source_code": str(status.get("code") or ""),
        }

    return {
        "schema": LATCH_SCHEMA,
        "ok": True,
        "code": LATCH_VALID_CODE,
        "state": "valid",
        "generated_at": now_utc(),
        "source_code": str(status.get("code") or ""),
        "boot_id": _read_boot_id(),
        "device_id": extract_vps_distribution_id(dict(status)) or extract_vps_distribution_id(dict(authorization)),
        "license_anchor": {
            "type": str(license_anchor.get("type") or ""),
            "value_stored_locally": False,
        },
        "device_authorization": {
            "stored": True,
            "path": str(authorization.get("path") or ""),
            "schema": str(authorization.get("schema") or ""),
            "key_id": str(authorization.get("key_id") or ""),
            "device_id": str(authorization.get("device_id") or ""),
            "device_public_key_sha256": str(authorization.get("device_public_key_sha256") or ""),
            "signature_hash": str(authorization.get("signature_hash") or ""),
            "not_before": str(authorization.get("not_before") or ""),
            "expires_at": str(authorization.get("expires_at") or ""),
            "permissions": safe_permissions,
        },
        "raw_dna_stored_locally": False,
    }


def write_device_auth_latch(status: Mapping[str, Any], path: str | os.PathLike[str] | None = None) -> dict[str, Any]:
    latch = build_device_auth_latch(status)
    target = Path(path or os.environ.get("RE_V5_DEVICE_AUTH_LATCH_STATUS", DEFAULT_AUTH_LATCH_STATUS_PATH))
    _atomic_write_json(target, latch)
    return latch


def refresh_device_auth_latch_from_local_authorization(
    *,
    auth_latch_status_path: str | os.PathLike[str] | None = None,
    register_status_path: str | os.PathLike[str] | None = None,
    device_auth_file: str | os.PathLike[str] | None = None,
    device_auth_public_key_file: str | os.PathLike[str] | None = None,
    device_private_key_file: str | os.PathLike[str] | None = None,
    device_public_key_file: str | os.PathLike[str] | None = None,
    sample_count: int = 3,
) -> dict[str, Any]:
    started_ns = time.monotonic_ns()
    target_latch = Path(auth_latch_status_path or os.environ.get("RE_V5_DEVICE_AUTH_LATCH_STATUS", DEFAULT_AUTH_LATCH_STATUS_PATH))
    target_register = Path(register_status_path or os.environ.get("RE_v5_device_dna_register_STATUS_PATH", DEFAULT_REGISTER_STATUS_PATH))
    args = SimpleNamespace(
        device_auth_file=str(device_auth_file or os.environ.get("AX8_DEVICE_AUTH_FILE", DEFAULT_DEVICE_AUTH_FILE)),
        device_auth_public_key_file=str(device_auth_public_key_file or os.environ.get("AX8_DEVICE_AUTH_PUBLIC_KEY_FILE", DEFAULT_DEVICE_AUTH_PUBLIC_KEY_FILE)),
        device_private_key_file=str(device_private_key_file or os.environ.get("AX8_DEVICE_PRIVATE_KEY_FILE", DEFAULT_DEVICE_PRIVATE_KEY_FILE)),
        device_public_key_file=str(device_public_key_file or os.environ.get("AX8_DEVICE_PUBLIC_KEY_FILE", DEFAULT_DEVICE_PUBLIC_KEY_FILE)),
    )
    try:
        from device_dna_register_auth import dna_hash_hex, existing_authorization_status, prepare_device_keypair

        dna_report = _read_consistent_live_dna(sample_count)
        dna = str(dna_report.get("value") or "")
        dna_hash = dna_hash_hex(dna)
        key_info = prepare_device_keypair(args, create=False)
        identity = require_registered_identity(target_register, dna_hash)
        auth_status = existing_authorization_status(args, dna, key_info)
        auth_status.update(identity)
        result: dict[str, Any] = {
            "ok": True,
            "code": "DEVICE_AUTH_LATCH_REFRESH_OK",
            "schema": "re-v5-device-auth-latch-refresh-v1",
            "generated_at": now_utc(),
            "message": "local device authorization verified and auth latch refreshed",
            "device_id": identity["vps_distribution_id"],
            "vps_distribution_id": identity["vps_distribution_id"],
            "vpsDistributionId": identity["vps_distribution_id"],
            "license_anchor": {
                "source": dna_report.get("source", ""),
                "type": dna_report.get("type", ""),
                "value_stored_locally": False,
                "hash": dna_hash,
                "hash_short": dna_hash[:8],
                "sample_count": int(dna_report.get("sample_count") or sample_count),
            },
            "device_authorization": auth_status,
            "device_public_key": {
                "path": args.device_public_key_file,
                "sha256": key_info.get("public_key_sha256", ""),
            },
            "raw_dna_stored_locally": False,
        }
    except Exception as exc:
        result = {
            "ok": False,
            "code": str(getattr(exc, "code", "") or "DEVICE_AUTH_LATCH_REFRESH_FAILED"),
            "schema": "re-v5-device-auth-latch-refresh-v1",
            "generated_at": now_utc(),
            "message": "local device authorization latch refresh failed",
            "error": f"{type(exc).__name__}: {exc}"[:500],
            "raw_dna_stored_locally": False,
        }

    try:
        latch = write_device_auth_latch(result, target_latch)
        result["auth_latch"] = {
            "ok": bool(latch.get("ok")),
            "code": latch.get("code"),
            "path": str(target_latch),
        }
    except Exception as exc:
        result["ok"] = False
        result["code"] = "DEVICE_AUTH_LATCH_WRITE_FAILED"
        result["auth_latch"] = {"ok": False, "path": str(target_latch), "error": f"{type(exc).__name__}: {exc}"[:500]}
    scrubbed_result = scrub_local_dna_fields(result)
    if isinstance(scrubbed_result, dict):
        result = scrubbed_result
    try:
        _atomic_write_json(target_register, result)
    except Exception as exc:
        result["register_status_write_error"] = f"{type(exc).__name__}: {exc}"[:500]
    result["elapsed_ms"] = int((time.monotonic_ns() - started_ns) / 1_000_000)
    return result

