from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any, Dict

VPS_DISTRIBUTION_ID_RE = re.compile(r"^[0-9]{6}$")
DNA_HASH_RE = re.compile(r"^[0-9a-f]{64}$")
LOCAL_DNA_SECRET_KEYS = {
    "deviceDna",
    "device_dna",
    "dnaHash",
    "pl_dna_hash",
    "plDnaHash",
    "plDeviceDna",
    "plDeviceDnaHash",
    "pl_device_dna",
    "pl_device_dna_hash",
    "dna_hash",
    "hash",
    "hashShort",
    "hash_short",
}


class VpsIdentityError(RuntimeError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


def normalize_vps_distribution_id(value: Any) -> str:
    text = str(value or "").strip()
    return text if VPS_DISTRIBUTION_ID_RE.match(text) else ""


def normalize_dna_hash(value: Any) -> str:
    text = str(value or "").strip().lower()
    return text if DNA_HASH_RE.match(text) else ""


def extract_vps_distribution_id(payload: Any) -> str:
    if not isinstance(payload, dict):
        return ""
    for key in ("vpsDistributionId", "vps_distribution_id", "device_id"):
        value = normalize_vps_distribution_id(payload.get(key))
        if value:
            return value
    for key in ("device", "device_authorization", "authorization", "payload"):
        nested = payload.get(key)
        if isinstance(nested, dict):
            value = extract_vps_distribution_id(nested)
            if value:
                return value
    return ""


def extract_dna_hash(payload: Any) -> str:
    if not isinstance(payload, dict):
        return ""
    for key in ("pl_dna_hash", "pl_device_dna_hash", "dna_hash"):
        value = normalize_dna_hash(payload.get(key))
        if value:
            return value
    anchor = payload.get("license_anchor")
    if isinstance(anchor, dict):
        value = normalize_dna_hash(anchor.get("hash"))
        if value:
            return value
    auth = payload.get("device_authorization")
    if isinstance(auth, dict):
        value = extract_dna_hash(auth)
        if value:
            return value
    return ""


def private_folder_name(vps_distribution_id: str, dna_hash: str) -> str:
    vps_id = normalize_vps_distribution_id(vps_distribution_id)
    pl_hash = normalize_dna_hash(dna_hash)
    if not vps_id or not pl_hash:
        raise VpsIdentityError("VPS_ID_DNA_INVALID", "VPS 分发ID或本机 DNA hash 无效")
    return f"{vps_id}-{pl_hash}"


def identity_fields(vps_distribution_id: str, dna_hash: str) -> Dict[str, str]:
    vps_distribution_id = normalize_vps_distribution_id(vps_distribution_id)
    dna_hash = normalize_dna_hash(dna_hash)
    if not vps_distribution_id or not dna_hash:
        raise VpsIdentityError("VPS_ID_DNA_INVALID", "VPS 鍒嗗彂ID鎴栨湰鏈?DNA hash 鏃犳晥")
    return {
        "vpsDistributionId": vps_distribution_id,
        "vps_distribution_id": vps_distribution_id,
        "device_id": vps_distribution_id,
        "pl_dna_hash": dna_hash,
    }


def public_identity_fields(vps_distribution_id: str) -> Dict[str, str]:
    vps_distribution_id = normalize_vps_distribution_id(vps_distribution_id)
    if not vps_distribution_id:
        raise VpsIdentityError("VPS_ID_INVALID", "VPS 分发ID无效")
    return {
        "vpsDistributionId": vps_distribution_id,
        "vps_distribution_id": vps_distribution_id,
        "device_id": vps_distribution_id,
    }


def scrub_local_dna_fields(value: Any) -> Any:
    if isinstance(value, dict):
        out: Dict[str, Any] = {}
        for key, item in value.items():
            if str(key) in LOCAL_DNA_SECRET_KEYS:
                continue
            out[key] = scrub_local_dna_fields(item)
        return out
    if isinstance(value, list):
        return [scrub_local_dna_fields(item) for item in value]
    return value


def load_register_status(path: str | Path) -> Dict[str, Any]:
    try:
        value = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    except Exception as exc:
        raise VpsIdentityError("DEVICE_NOT_REGISTERED", "请先登记本机码，再下载授权或服务器驱动") from exc
    if not isinstance(value, dict):
        raise VpsIdentityError("DEVICE_NOT_REGISTERED", "本机码登记状态无效，请重新登记本机码")
    return value


def require_registered_identity(path: str | Path, dna_hash: str) -> Dict[str, str]:
    expected_hash = normalize_dna_hash(dna_hash)
    status = load_register_status(path)
    vps_id = extract_vps_distribution_id(status)
    if not vps_id:
        raise VpsIdentityError("DEVICE_NOT_REGISTERED", "请先登记本机码，取得 VPS 分发ID")
    if not expected_hash:
        raise VpsIdentityError("DEVICE_DNA_HASH_INVALID", "本机 DNA hash 无效")
    return identity_fields(vps_id, expected_hash)
