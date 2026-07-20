#!/usr/bin/env python3
"""Drive profile publishing helpers for the 8AX VPS admin API."""

from __future__ import annotations

import hashlib
import json
import re
import tempfile
from dataclasses import dataclass
from http import HTTPStatus
from pathlib import Path
from typing import Any

from vps_private_binding import validate_private_device_binding


DRIVE_PROFILE_SCHEMA = "v5-driver-profile-map-v1"
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
VPS_ID_RE = re.compile(r"^[0-9]{6}$")


@dataclass(frozen=True)
class DriveProfilePublishError(RuntimeError):
    status: HTTPStatus
    message: str


def private_folder_name(private_id: str, private_hash: str) -> str:
    if not VPS_ID_RE.fullmatch(private_id):
        raise DriveProfilePublishError(HTTPStatus.BAD_REQUEST, "privateId must be a 6-digit VPS distribution id")
    if not SHA256_RE.fullmatch(private_hash):
        raise DriveProfilePublishError(HTTPStatus.BAD_REQUEST, "privateHash must be a 64-char sha256 hex digest")
    return f"{private_id}-{private_hash.lower()}"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require_text(fields: dict[str, str], name: str) -> str:
    value = (fields.get(name) or "").strip()
    if not value:
        raise DriveProfilePublishError(HTTPStatus.BAD_REQUEST, f"{name} is required")
    return value


def require_size(fields: dict[str, str], name: str) -> int:
    value = require_text(fields, name)
    try:
        parsed = int(value, 10)
    except ValueError as exc:
        raise DriveProfilePublishError(HTTPStatus.BAD_REQUEST, f"{name} must be an integer") from exc
    if parsed <= 0:
        raise DriveProfilePublishError(HTTPStatus.BAD_REQUEST, f"{name} must be positive")
    return parsed


def atomic_write_bytes(target: Path, data: bytes) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=target.name + ".", suffix=".tmp", dir=str(target.parent))
    tmp = Path(tmp_name)
    try:
        with open(fd, "wb", closefd=True) as handle:
            handle.write(data)
            handle.flush()
        tmp.replace(target)
    finally:
        if tmp.exists():
            tmp.unlink()


def write_sha256_sidecar(target: Path, digest: str) -> None:
    atomic_write_bytes(target.with_name(target.name + ".sha256"), f"{digest}  {target.name}\n".encode("ascii"))


def validate_profile_payload(data: bytes, scope: str) -> dict[str, Any]:
    try:
        payload = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise DriveProfilePublishError(HTTPStatus.BAD_REQUEST, "profile file must be valid UTF-8 JSON") from exc
    if not isinstance(payload, dict):
        raise DriveProfilePublishError(HTTPStatus.BAD_REQUEST, "profile file must be a JSON object")
    if payload.get("schema") != DRIVE_PROFILE_SCHEMA:
        raise DriveProfilePublishError(HTTPStatus.BAD_REQUEST, f"profile schema must be {DRIVE_PROFILE_SCHEMA}")
    if str(payload.get("map_scope") or "").strip().lower() != scope:
        raise DriveProfilePublishError(HTTPStatus.BAD_REQUEST, "profile map_scope must match publish scope")
    if not isinstance(payload.get("profiles"), list):
        raise DriveProfilePublishError(HTTPStatus.BAD_REQUEST, "profile profiles must be a list")
    return payload


def publish_drive_profile(
    fields: dict[str, str],
    profile_path: Path,
    profile_filename: str,
    *,
    storage_root: Path,
    static_root: Path,
    legacy_static_root: Path,
    private_root: Path,
    validate_private_binding: bool = True,
) -> dict[str, Any]:
    scope = require_text(fields, "scope").lower()
    if scope not in {"public", "private"}:
        raise DriveProfilePublishError(HTTPStatus.BAD_REQUEST, "scope must be public or private")
    if Path(profile_filename or "").name != "driver_profile_map.json":
        raise DriveProfilePublishError(HTTPStatus.BAD_REQUEST, "profile filename must be driver_profile_map.json")

    data = profile_path.read_bytes()
    profile_sha = require_text(fields, "profileSha256").lower()
    if not SHA256_RE.fullmatch(profile_sha):
        raise DriveProfilePublishError(HTTPStatus.BAD_REQUEST, "profileSha256 must be a 64-char sha256 hex digest")
    profile_size = require_size(fields, "profileSizeBytes")
    if len(data) != profile_size:
        raise DriveProfilePublishError(HTTPStatus.BAD_REQUEST, "profile size mismatch")
    if sha256_bytes(data) != profile_sha:
        raise DriveProfilePublishError(HTTPStatus.BAD_REQUEST, "profile sha256 mismatch")
    validate_profile_payload(data, scope)

    private_hash = ""
    private_id = ""
    if scope == "private":
        private_id = require_text(fields, "privateId")
        private_hash = require_text(fields, "privateHash").lower()
        if validate_private_binding:
            validate_private_device_binding(private_id, private_hash, DriveProfilePublishError)
        private_folder = private_folder_name(private_id, private_hash)
        target_rel = f"private/{private_folder}/driver_profile_map.json"
        targets = [private_root / private_folder / "driver_profile_map.json"]
    else:
        target_rel = "public/driver_profile_map.json"
        targets = [
            storage_root / "public" / "driver_profile_map.json",
            static_root / "public" / "driver_profile_map.json",
            legacy_static_root / "public" / "driver_profile_map.json",
        ]

    for target in targets:
        atomic_write_bytes(target, data)
        write_sha256_sidecar(target, profile_sha)

    return {
        "success": True,
        "message": "Drive profile published to VPS storage.",
        "sourceScope": scope,
        "targetRel": target_rel,
        "vpsDistributionId": private_id,
        "dnaBinding": "server_verified" if scope == "private" else "",
        "plDnaHash": "",
        "profileSha256": profile_sha,
        "profileSizeBytes": profile_size,
    }
