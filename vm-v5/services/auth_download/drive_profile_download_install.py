from __future__ import annotations

import hashlib
import io
import tarfile
import zipfile
from pathlib import Path
from typing import Any, Dict, List, Mapping, Optional
from drive_profile_download_core import atomic_write, load_json_bytes, parse_sha256_sidecar
from drive_profile_download_errors import DownloadError

RELEASE_CONTRACT_KEY = "release_contract"


def safe_rel_path(name: str) -> Optional[Path]:
    rel = Path(str(name or "").replace("\\", "/"))
    if rel.is_absolute() or ".." in rel.parts:
        return None
    allowed = {"manifest.json", "driver_profile_map.json", "driver_profile_map.json.sha256"}
    if str(rel) in allowed or (len(rel.parts) >= 2 and rel.parts[0] in ("driver_profiles", "source_catalogs")):
        return rel
    return None

def install_zip(data: bytes, out_dir: Path) -> List[str]:
    installed: List[str] = []
    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        for info in zf.infolist():
            if info.is_dir():
                continue
            rel = safe_rel_path(info.filename)
            if rel is None:
                continue
            target = out_dir / rel
            atomic_write(target, zf.read(info))
            installed.append(str(rel))
    return installed

def install_tar(data: bytes, out_dir: Path) -> List[str]:
    installed: List[str] = []
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:*") as tf:
        for member in tf.getmembers():
            if not member.isfile():
                continue
            rel = safe_rel_path(member.name)
            if rel is None:
                continue
            src = tf.extractfile(member)
            if src is None:
                continue
            target = out_dir / rel
            atomic_write(target, src.read())
            installed.append(str(rel))
    return installed

def install_json_payload(data: bytes, out_dir: Path, artifact_name: str) -> List[str]:
    payload = load_json_bytes(data, artifact_name)
    if isinstance(payload.get("driver_profile_map"), dict):
        raise DownloadError("%s 是包装 JSON，不能字节保真保存 driver_profile_map；请使用直接映射表 JSON。" % artifact_name)
    if isinstance(payload.get("profiles"), list) and (
        "schema_version" in payload or "schema" in payload or artifact_name == "driver_profile_map.json"
    ):
        if artifact_name == "driver_profile_map.json":
            scope = out_dir.name.lower()
            if scope in ("public", "private"):
                errors = validate_profile_map(payload, scope, require_nonempty=(scope == "public"))
                if errors:
                    raise DownloadError("%s 完整性校验失败: %s" % (scope, ",".join(errors)))
        atomic_write(out_dir / artifact_name, data)
        return [artifact_name]
    atomic_write(out_dir / artifact_name, data)
    return [artifact_name]

def install_payload(data: bytes, content_type: str, out_dir: Path, artifact_name: str) -> List[str]:
    lower_type = (content_type or "").lower()
    archive_payload = data[:2] == b"PK" or "zip" in lower_type or data[:2] == b"\x1f\x8b" or "tar" in lower_type or "gzip" in lower_type
    if artifact_name == "driver_profile_map.json" and archive_payload:
        raise DownloadError("driver_profile_map.json 服务器下载只允许直接 JSON；压缩包会破坏 SHA 字节保真。")
    if data[:2] == b"PK" or "zip" in lower_type:
        return install_zip(data, out_dir)
    if data[:2] == b"\x1f\x8b" or "tar" in lower_type or "gzip" in lower_type:
        return install_tar(data, out_dir)
    return install_json_payload(data, out_dir, artifact_name)

def read_map(path: Path) -> Dict[str, Any]:
    if not path.exists():
        return {}
    return load_json_bytes(path.read_bytes(), str(path))

def sha256_file(path: Path) -> str:
    if not path.exists():
        return ""
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest().upper()

def profile_count(payload: Dict[str, Any]) -> int:
    profiles = payload.get("profiles", []) if isinstance(payload, dict) else []
    return len(profiles) if isinstance(profiles, list) else 0

def _nonempty_string(value: Any) -> bool:
    return isinstance(value, str) and bool(value.strip())

def validate_release_contract(payload: Dict[str, Any], scope: str) -> List[str]:
    errors: List[str] = []
    if not _nonempty_string(payload.get("map_version")):
        errors.append("%s_map_version_missing" % scope)
    contract = payload.get(RELEASE_CONTRACT_KEY)
    if not isinstance(contract, dict):
        errors.append("%s_release_contract_missing" % scope)
        return errors
    firmware_range = contract.get("firmware_range")
    if not isinstance(firmware_range, dict) or not (
        _nonempty_string(firmware_range.get("min")) or _nonempty_string(firmware_range.get("max"))
    ):
        errors.append("%s_firmware_range_missing" % scope)
    object_dictionary_range = contract.get("object_dictionary_range")
    if not isinstance(object_dictionary_range, dict):
        errors.append("%s_object_dictionary_range_missing" % scope)
    else:
        for key in ("vendor_id", "product_code"):
            if not _nonempty_string(object_dictionary_range.get(key)):
                errors.append("%s_object_dictionary_%s_missing" % (scope, key))
    authorized_features = contract.get("authorized_features")
    if not isinstance(authorized_features, list) or not any(_nonempty_string(item) for item in authorized_features):
        errors.append("%s_authorized_features_missing" % scope)
    rollback = contract.get("rollback")
    if not isinstance(rollback, dict) or not isinstance(rollback.get("supported"), bool):
        errors.append("%s_rollback_contract_missing" % scope)
    return errors

def validate_profile_commands(profile: Mapping[str, Any], scope: str, profile_index: int) -> List[str]:
    errors: List[str] = []
    commands = profile.get("commands")
    if not isinstance(commands, dict) or not commands:
        return ["%s_profile_%d_commands_missing" % (scope, profile_index)]
    for command_name, command in commands.items():
        name = str(command_name or "").strip()
        if not name.startswith("drive."):
            errors.append("%s_profile_%d_command_%s_name_invalid" % (scope, profile_index, name or "empty"))
            continue
        if not isinstance(command, dict):
            errors.append("%s_profile_%d_command_%s_not_object" % (scope, profile_index, name))
            continue
        standard_command = str(command.get("standard_command") or "").strip()
        if standard_command and standard_command != name:
            errors.append("%s_profile_%d_command_%s_standard_mismatch:%s" % (scope, profile_index, name, standard_command))
        supported = command.get("supported")
        if not isinstance(supported, bool):
            errors.append("%s_profile_%d_command_%s_supported_not_bool" % (scope, profile_index, name))
        obj = command.get("object")
        if not isinstance(obj, dict):
            errors.append("%s_profile_%d_command_%s_object_missing" % (scope, profile_index, name))
        access = str(command.get("access") or "").strip().lower()
        mutates_drive = (
            supported is True and
            (access in {"write", "rw", "read_write"} or
                name.startswith("drive.set_") or
                name.startswith("drive.write_") or
                name.startswith("drive.restore_") or
                name.startswith("drive.reset_") or
                name.startswith("drive.clear_") or
                name.startswith("drive.save_"))
        )
        if mutates_drive and not (
            command.get("verify_object") is not None or
            command.get("verify_expected") is not None or
            command.get("verify_tolerance") is not None
        ):
            errors.append("%s_profile_%d_command_%s_verify_missing" % (scope, profile_index, name))
    return errors

def validate_profile_map(payload: Dict[str, Any], scope: str, require_nonempty: bool) -> List[str]:
    errors: List[str] = []
    if not isinstance(payload, dict) or not payload:
        return ["%s_map_missing" % scope]
    schema = str(payload.get("schema") or "").strip()
    if schema != "v5-driver-profile-map-v1":
        errors.append("%s_schema_invalid:%s" % (scope, schema or "missing"))
    map_scope = str(payload.get("map_scope") or "").strip().lower()
    if map_scope and map_scope != scope:
        errors.append("%s_scope_mismatch:%s" % (scope, map_scope))
    errors.extend(validate_release_contract(payload, scope))
    profiles = payload.get("profiles")
    if not isinstance(profiles, list):
        errors.append("%s_profiles_not_list" % scope)
        return errors
    if require_nonempty and not profiles:
        errors.append("%s_profiles_empty" % scope)
    for index, item in enumerate(profiles):
        if not isinstance(item, dict):
            errors.append("%s_profile_%d_not_object" % (scope, index))
            continue
        identity = (
            item.get("profile_id")
            or item.get("vendor_id")
            or item.get("product_code")
            or item.get("model")
            or item.get("name_pattern")
        )
        if not str(identity or "").strip():
            errors.append("%s_profile_%d_identity_missing" % (scope, index))
        errors.extend(validate_profile_commands(item, scope, index))
    return errors

def release_contract_status(payload: Dict[str, Any]) -> Dict[str, Any]:
    contract = payload.get(RELEASE_CONTRACT_KEY) if isinstance(payload, dict) else None
    if not isinstance(contract, dict):
        return {"present": False}
    firmware_range = contract.get("firmware_range") if isinstance(contract.get("firmware_range"), dict) else {}
    object_dictionary_range = (
        contract.get("object_dictionary_range") if isinstance(contract.get("object_dictionary_range"), dict) else {}
    )
    rollback = contract.get("rollback") if isinstance(contract.get("rollback"), dict) else {}
    authorized_features = contract.get("authorized_features")
    if not isinstance(authorized_features, list):
        authorized_features = []
    return {
        "present": True,
        "firmware_range": {
            "min": str(firmware_range.get("min") or ""),
            "max": str(firmware_range.get("max") or ""),
        },
        "object_dictionary_range": {
            "vendor_id": str(object_dictionary_range.get("vendor_id") or ""),
            "product_code": str(object_dictionary_range.get("product_code") or ""),
            "revision_min": str(object_dictionary_range.get("revision_min") or ""),
            "revision_max": str(object_dictionary_range.get("revision_max") or ""),
        },
        "authorized_features": [str(item) for item in authorized_features if _nonempty_string(item)],
        "rollback": {
            "supported": bool(rollback.get("supported")) if isinstance(rollback.get("supported"), bool) else False,
            "previous_map_version": str(rollback.get("previous_map_version") or ""),
        },
    }

def map_cache_status(path: Path, payload: Dict[str, Any], result: Dict[str, Any], scope: str) -> Dict[str, Any]:
    exists = path.is_file()
    fresh = bool(result.get("ok"))
    local_sha256 = sha256_file(path)
    sidecar_path = path.with_name(path.name + ".sha256")
    declared_sha256 = ""
    if sidecar_path.is_file():
        try:
            declared_sha256 = parse_sha256_sidecar(sidecar_path.read_bytes(), str(sidecar_path))
        except Exception:
            declared_sha256 = ""
    if exists and fresh:
        status = "updated"
    elif exists:
        status = "cached"
    else:
        status = "absent"
    return {
        "scope": scope,
        "status": status,
        "path": str(path),
        "exists": exists,
        "sha256": local_sha256,
        "local_sha256": local_sha256,
        "remote_sha256": str(result.get("remote_sha256") or declared_sha256),
        "sha256_sidecar_path": str(sidecar_path),
        "sha256_sidecar_exists": sidecar_path.is_file(),
        "sha256_verified": bool(local_sha256 and declared_sha256 and local_sha256.upper() == declared_sha256.upper()),
        "profile_count": profile_count(payload),
        "map_version": str(payload.get("map_version") or ""),
        "schema": str(payload.get("schema") or ""),
        "release_contract": release_contract_status(payload),
        "fresh": fresh,
    }
