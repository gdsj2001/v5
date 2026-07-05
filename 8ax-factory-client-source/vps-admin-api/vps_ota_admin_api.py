#!/usr/bin/env python3
"""Minimal VPS admin API for publishing 8AX OTA packages.

This service intentionally only publishes signed OTA artifacts to the VPS truth
store and static mirror. It does not sign packages, select board scopes, start a
board upgrade, or write board state.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import posixpath
import re
import shutil
import sys
import tempfile
from dataclasses import dataclass
from datetime import UTC
from datetime import datetime
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler
from http.server import ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

from vps_drive_profile_admin import DriveProfilePublishError
from vps_drive_profile_admin import publish_drive_profile
from vps_private_binding import validate_private_device_binding

SCHEMA = "re.v3.ota_manifest.v1"
SCOPE_POLICY = "dna_private_first_no_public_when_private_present"
DEFAULT_STORAGE_ROOT = Path("/opt/8ax-auth/storage/ota")
DEFAULT_STATIC_ROOT = Path("/var/www/html/updates/ota")
DEFAULT_PRIVATE_ROOT = Path("/opt/8ax-auth/storage/private")
DEFAULT_DRIVE_STORAGE_ROOT = Path("/opt/8ax-auth/storage/drive-profiles")
DEFAULT_DRIVE_STATIC_ROOT = Path("/var/www/html/updates/drive-profiles")
DEFAULT_DRIVE_LEGACY_STATIC_ROOT = Path("/var/www/html/drive-profiles")
SAFE_SEGMENT_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
VPS_ID_RE = re.compile(r"^[0-9]{6}$")
PACKAGE_EXTENSIONS = (".ota", ".zip", ".tar", ".tar.gz", ".tgz")
SIGNATURE_EXTENSIONS = (".sig", ".signature")
TEXT_FIELDS = tuple(
    "scope privateId privateHash profileSha256 profileSizeBytes product channel version "
    "packageSha256 signatureSha256 packageSizeBytes signatureSizeBytes signatureAlg keyId "
    "minCompatibleVersion antiRollbackMinVersion productProfile hardwareProfile reason scopePolicy".split()
)
class PublishError(RuntimeError):
    def __init__(self, status: HTTPStatus, message: str):
        super().__init__(message)
        self.status = status
        self.message = message

@dataclass(frozen=True)
class PublishConfig:
    storage_root: Path = DEFAULT_STORAGE_ROOT
    static_root: Path = DEFAULT_STATIC_ROOT
    private_root: Path = DEFAULT_PRIVATE_ROOT
    drive_storage_root: Path = DEFAULT_DRIVE_STORAGE_ROOT
    drive_static_root: Path = DEFAULT_DRIVE_STATIC_ROOT
    drive_legacy_static_root: Path = DEFAULT_DRIVE_LEGACY_STATIC_ROOT
    admin_user: str = ""
    admin_password: str = ""
    max_upload_bytes: int = 2 * 1024 * 1024 * 1024

def utc_stamp(now: datetime | None = None) -> str:
    value = now or datetime.now(UTC)
    return value.astimezone(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")

def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def require_text(fields: dict[str, str], name: str) -> str:
    value = (fields.get(name) or "").strip()
    if not value:
        raise PublishError(HTTPStatus.BAD_REQUEST, f"{name} is required")
    return value

def require_segment(fields: dict[str, str], name: str) -> str:
    value = require_text(fields, name)
    if not SAFE_SEGMENT_RE.fullmatch(value):
        raise PublishError(HTTPStatus.BAD_REQUEST, f"{name} must be a safe path segment")
    return value

def require_sha256(fields: dict[str, str], name: str) -> str:
    value = require_text(fields, name).lower()
    if not SHA256_RE.fullmatch(value):
        raise PublishError(HTTPStatus.BAD_REQUEST, f"{name} must be a 64-char sha256 hex digest")
    return value

def require_size(fields: dict[str, str], name: str) -> int:
    value = require_text(fields, name)
    try:
        parsed = int(value, 10)
    except ValueError as exc:
        raise PublishError(HTTPStatus.BAD_REQUEST, f"{name} must be an integer") from exc
    if parsed <= 0:
        raise PublishError(HTTPStatus.BAD_REQUEST, f"{name} must be positive")
    return parsed

def safe_filename(value: str, allowed_extensions: tuple[str, ...], field_name: str) -> str:
    name = Path(value or "").name.strip()
    if not name or name in {".", ".."} or "/" in name or "\\" in name:
        raise PublishError(HTTPStatus.BAD_REQUEST, f"{field_name} filename is invalid")
    lower = name.lower()
    if not any(lower.endswith(ext) for ext in allowed_extensions):
        raise PublishError(HTTPStatus.BAD_REQUEST, f"{field_name} extension is not allowed")
    if len(name) > 160:
        raise PublishError(HTTPStatus.BAD_REQUEST, f"{field_name} filename is too long")
    return name

def atomic_write_bytes(target: Path, data: bytes) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=target.name + ".", suffix=".tmp", dir=str(target.parent))
    tmp = Path(tmp_name)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(tmp, target)
    finally:
        if tmp.exists():
            tmp.unlink()

def copy_file_verified(source: Path, target: Path, expected_sha256: str, expected_size: int) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=target.name + ".", suffix=".tmp", dir=str(target.parent))
    tmp = Path(tmp_name)
    digest = hashlib.sha256()
    total = 0
    try:
        with source.open("rb") as src, os.fdopen(fd, "wb") as dst:
            for chunk in iter(lambda: src.read(1024 * 1024), b""):
                total += len(chunk)
                digest.update(chunk)
                dst.write(chunk)
            dst.flush()
            os.fsync(dst.fileno())
        actual_sha = digest.hexdigest()
        if total != expected_size:
            raise PublishError(HTTPStatus.BAD_REQUEST, f"{source.name} size mismatch: {total} != {expected_size}")
        if actual_sha != expected_sha256:
            raise PublishError(HTTPStatus.BAD_REQUEST, f"{source.name} sha256 mismatch: {actual_sha} != {expected_sha256}")
        os.replace(tmp, target)
    finally:
        if tmp.exists():
            tmp.unlink()

def write_sha256_sidecar(target: Path, digest: str) -> None:
    atomic_write_bytes(target.with_name(target.name + ".sha256"), f"{digest}  {target.name}\n".encode("ascii"))

def canonical_manifest_bytes(manifest: dict[str, Any]) -> bytes:
    return (json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")

def private_folder_name(private_id: str, private_hash: str) -> str:
    if not VPS_ID_RE.fullmatch(private_id):
        raise PublishError(HTTPStatus.BAD_REQUEST, "privateId is required for private scope")
    if not SHA256_RE.fullmatch(private_hash):
        raise PublishError(HTTPStatus.BAD_REQUEST, "privateHash is required for private scope")
    return f"{private_id}-{private_hash.lower()}"

def build_target_rel(scope: str, private_folder: str, product: str, channel: str) -> str:
    if scope == "private":
        if not re.fullmatch(r"[0-9]{6}-[0-9a-f]{64}", private_folder):
            raise PublishError(HTTPStatus.BAD_REQUEST, "private target must use id-dna folder")
        return posixpath.join("private", private_folder, "ota", product, channel)
    return posixpath.join("public", product, channel)

def publish_ota_package(
    fields: dict[str, str],
    package_path: Path,
    package_filename: str,
    signature_path: Path,
    signature_filename: str,
    *,
    storage_root: Path = DEFAULT_STORAGE_ROOT,
    static_root: Path = DEFAULT_STATIC_ROOT,
    private_root: Path = DEFAULT_PRIVATE_ROOT,
    validate_private_binding: bool = True,
    now: datetime | None = None,
) -> dict[str, Any]:
    scope = require_text(fields, "scope").lower()
    if scope not in {"public", "private"}:
        raise PublishError(HTTPStatus.BAD_REQUEST, "scope must be public or private")
    private_hash = (fields.get("privateHash") or "").strip().lower()
    private_id = (fields.get("privateId") or "").strip()
    if scope == "private" and not SHA256_RE.fullmatch(private_hash):
        raise PublishError(HTTPStatus.BAD_REQUEST, "privateHash is required for private scope")
    if scope == "private" and not VPS_ID_RE.fullmatch(private_id):
        raise PublishError(HTTPStatus.BAD_REQUEST, "privateId is required for private scope")
    if scope == "public":
        private_hash = ""
        private_id = ""
    elif validate_private_binding:
        validate_private_device_binding(private_id, private_hash, PublishError)
    product = require_segment(fields, "product")
    channel = require_segment(fields, "channel")
    version = require_segment(fields, "version")
    signature_alg = require_segment(fields, "signatureAlg")
    key_id = require_segment(fields, "keyId")
    min_compatible = require_segment(fields, "minCompatibleVersion")
    anti_rollback = require_segment(fields, "antiRollbackMinVersion")
    product_profile = require_segment(fields, "productProfile")
    hardware_profile = require_segment(fields, "hardwareProfile")
    reason = require_text(fields, "reason")
    if len(reason) > 4096:
        raise PublishError(HTTPStatus.BAD_REQUEST, "reason is too long")
    scope_policy = require_text(fields, "scopePolicy")
    if scope_policy != SCOPE_POLICY:
        raise PublishError(HTTPStatus.BAD_REQUEST, "scopePolicy is not accepted")

    package_sha = require_sha256(fields, "packageSha256")
    signature_sha = require_sha256(fields, "signatureSha256")
    package_size = require_size(fields, "packageSizeBytes")
    signature_size = require_size(fields, "signatureSizeBytes")
    package_name = safe_filename(package_filename, PACKAGE_EXTENSIONS, "package")
    signature_name = safe_filename(signature_filename, SIGNATURE_EXTENSIONS, "signature")

    private_folder = private_folder_name(private_id, private_hash) if scope == "private" else ""
    target_rel = build_target_rel(scope, private_folder, product, channel)
    release_rel = posixpath.join(target_rel, "releases", version)
    storage_release = private_root / Path(release_rel[len("private/"):]) if scope == "private" else storage_root / Path(release_rel)
    static_release = static_root / Path(release_rel)
    storage_channel = private_root / Path(target_rel[len("private/"):]) if scope == "private" else storage_root / Path(target_rel)
    static_channel = static_root / Path(target_rel)
    package_rel = posixpath.join("releases", version, package_name)
    signature_rel = posixpath.join("releases", version, signature_name)

    for root in (storage_release, static_release, storage_channel, static_channel):
        root.mkdir(parents=True, exist_ok=True)

    copy_file_verified(package_path, storage_release / package_name, package_sha, package_size)
    copy_file_verified(signature_path, storage_release / signature_name, signature_sha, signature_size)
    copy_file_verified(package_path, static_release / package_name, package_sha, package_size)
    copy_file_verified(signature_path, static_release / signature_name, signature_sha, signature_size)
    for target, digest in (
        (storage_release / package_name, package_sha),
        (storage_release / signature_name, signature_sha),
        (static_release / package_name, package_sha),
        (static_release / signature_name, signature_sha),
    ):
        write_sha256_sidecar(target, digest)

    published_at = utc_stamp(now)
    manifest = {
        "schema": SCHEMA,
        "source_scope": scope,
        "vps_distribution_id": private_id,
        "dna_binding": "server_verified" if scope == "private" else "",
        "private_folder": private_folder,
        "product": product,
        "channel": channel,
        "version": version,
        "package": {
            "file": package_rel,
            "filename": package_name,
            "sha256": package_sha,
            "size_bytes": package_size,
        },
        "signature": {
            "file": signature_rel,
            "filename": signature_name,
            "sha256": signature_sha,
            "size_bytes": signature_size,
            "alg": signature_alg,
            "key_id": key_id,
        },
        "min_compatible_version": min_compatible,
        "anti_rollback_min_version": anti_rollback,
        "product_profile": product_profile,
        "hardware_profile": hardware_profile,
        "scope_policy": scope_policy,
        "selection_policy": "private package blocks public fallback",
        "reason": reason,
        "published_at_utc": published_at,
    }
    manifest_bytes = canonical_manifest_bytes(manifest)
    manifest_sha = sha256_bytes(manifest_bytes)
    for channel_dir in (storage_channel, static_channel):
        manifest_path = channel_dir / "manifest.json"
        atomic_write_bytes(manifest_path, manifest_bytes)
        write_sha256_sidecar(manifest_path, manifest_sha)

    return {
        "success": True,
        "message": "OTA package published to VPS storage and static mirror.",
        "sourceScope": scope,
        "targetRel": target_rel,
        "vpsDistributionId": private_id,
        "dnaBinding": "server_verified" if scope == "private" else "",
        "privateFolder": private_folder,
        "releaseRel": release_rel,
        "manifestSha256": manifest_sha,
        "packageSha256": package_sha,
        "signatureSha256": signature_sha,
        "manifest": manifest,
    }

def load_config(args: argparse.Namespace) -> PublishConfig:
    def env_first(*names: str) -> str:
        for name in names:
            value = os.environ.get(name, "").strip()
            if value:
                return value
        return ""

    return PublishConfig(
        storage_root=Path(args.storage_root),
        static_root=Path(args.static_root),
        private_root=Path(args.private_root),
        drive_storage_root=Path(args.drive_storage_root),
        drive_static_root=Path(args.drive_static_root),
        drive_legacy_static_root=Path(args.drive_legacy_static_root),
        admin_user=env_first("AX8_VPS_ADMIN_USER", "AX8_ADMIN_USER"),
        admin_password=env_first("AX8_VPS_ADMIN_PASSWORD", "AX8_ADMIN_PASSWORD"),
        max_upload_bytes=int(os.environ.get("AX8_OTA_MAX_UPLOAD_BYTES", str(2 * 1024 * 1024 * 1024))),
    )

def basic_auth_ok(header: str | None, config: PublishConfig) -> bool:
    if not config.admin_user or not config.admin_password:
        return False
    if not header or not header.startswith("Basic "):
        return False
    try:
        raw = base64.b64decode(header[6:], validate=True).decode("utf-8")
    except Exception:
        return False
    user, sep, password = raw.partition(":")
    return bool(sep) and user == config.admin_user and password == config.admin_password

def make_handler(config: PublishConfig) -> type[BaseHTTPRequestHandler]:
    class OtaAdminHandler(BaseHTTPRequestHandler):
        server_version = "8axOtaAdmin/0.1"

        def do_GET(self) -> None:
            if urlparse(self.path).path == "/healthz":
                self.write_json(HTTPStatus.OK, {"status": "ok", "service": "8ax-ota-admin-api", "time": utc_stamp()})
                return
            self.write_json(HTTPStatus.NOT_FOUND, {"success": False, "message": "not found"})

        def do_POST(self) -> None:
            path = urlparse(self.path).path
            if path not in {"/api/v1/admin/ota/packages", "/api/v1/admin/drive-profiles"}:
                self.write_json(HTTPStatus.NOT_FOUND, {"success": False, "message": "not found"})
                return
            if not config.admin_user or not config.admin_password:
                self.write_json(HTTPStatus.SERVICE_UNAVAILABLE, {"success": False, "message": "admin auth is not configured"})
                return
            if not basic_auth_ok(self.headers.get("Authorization"), config):
                self.send_response(HTTPStatus.UNAUTHORIZED)
                self.send_header("WWW-Authenticate", 'Basic realm="8ax-vps-admin"')
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.end_headers()
                self.wfile.write(json.dumps({"success": False, "message": "unauthorized"}).encode("utf-8"))
                return
            length = int(self.headers.get("Content-Length") or "0")
            if length <= 0 or length > config.max_upload_bytes:
                self.write_json(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, {"success": False, "message": "upload size is not accepted"})
                return
            try:
                response = self.handle_drive_profile_publish() if path == "/api/v1/admin/drive-profiles" else self.handle_publish()
            except PublishError as exc:
                self.write_json(exc.status, {"success": False, "message": exc.message})
                return
            except DriveProfilePublishError as exc:
                self.write_json(exc.status, {"success": False, "message": exc.message})
                return
            except Exception as exc:
                self.write_json(HTTPStatus.INTERNAL_SERVER_ERROR, {"success": False, "message": f"{type(exc).__name__}: {exc}"})
                return
            self.write_json(HTTPStatus.OK, response)

        def handle_publish(self) -> dict[str, Any]:
            with tempfile.TemporaryDirectory(prefix="8ax_ota_admin_upload_") as tmp:
                fields, package_path, package_name, signature_path, signature_name = self.parse_multipart(Path(tmp))
                return publish_ota_package(
                    fields,
                    package_path,
                    package_name,
                    signature_path,
                    signature_name,
                    storage_root=config.storage_root,
                    static_root=config.static_root,
                    private_root=config.private_root,
                )

        def handle_drive_profile_publish(self) -> dict[str, Any]:
            with tempfile.TemporaryDirectory(prefix="8ax_drive_profile_upload_") as tmp:
                fields, profile_path, profile_name = self.parse_drive_profile_multipart(Path(tmp))
                return publish_drive_profile(
                    fields,
                    profile_path,
                    profile_name,
                    storage_root=config.drive_storage_root,
                    static_root=config.drive_static_root,
                    legacy_static_root=config.drive_legacy_static_root,
                    private_root=config.private_root,
                )

        def parse_multipart(self, tmp: Path) -> tuple[dict[str, str], Path, str, Path, str]:
            form = self.read_multipart_form()
            fields = self.form_text_fields(form)
            package_item = form["package"] if "package" in form else None
            signature_item = form["signature"] if "signature" in form else None
            if package_item is None or signature_item is None:
                raise PublishError(HTTPStatus.BAD_REQUEST, "package and signature file parts are required")
            package_path = self.copy_field_storage_file(package_item, tmp / "package.bin")
            signature_path = self.copy_field_storage_file(signature_item, tmp / "signature.bin")
            return fields, package_path, str(package_item.filename or "package.ota"), signature_path, str(signature_item.filename or "package.sig")

        def parse_drive_profile_multipart(self, tmp: Path) -> tuple[dict[str, str], Path, str]:
            form = self.read_multipart_form()
            fields = self.form_text_fields(form)
            profile_item = form["profile"] if "profile" in form else None
            if profile_item is None:
                raise PublishError(HTTPStatus.BAD_REQUEST, "profile file part is required")
            profile_path = self.copy_field_storage_file(profile_item, tmp / "driver_profile_map.json")
            return fields, profile_path, str(profile_item.filename or "driver_profile_map.json")

        @staticmethod
        def copy_field_storage_file(item: Any, target: Path) -> Path:
            source = getattr(item, "file", None)
            if source is None:
                raise PublishError(HTTPStatus.BAD_REQUEST, "file part is missing data")
            source.seek(0)
            with target.open("wb") as handle:
                shutil.copyfileobj(source, handle, 1024 * 1024)
            return target

        def read_multipart_form(self) -> Any:
            import warnings

            with warnings.catch_warnings():
                warnings.simplefilter("ignore", DeprecationWarning)
                import cgi

            environ = {
                "REQUEST_METHOD": "POST",
                "CONTENT_TYPE": self.headers.get("Content-Type", ""),
                "CONTENT_LENGTH": self.headers.get("Content-Length", "0"),
            }
            return cgi.FieldStorage(fp=self.rfile, headers=self.headers, environ=environ, keep_blank_values=True)

        @staticmethod
        def form_text_fields(form: Any) -> dict[str, str]:
            fields: dict[str, str] = {}
            for name in TEXT_FIELDS:
                value = form.getfirst(name, "")
                fields[name] = value if isinstance(value, str) else str(value or "")
            return fields

        def write_json(self, status: HTTPStatus, payload: dict[str, Any]) -> None:
            data = json.dumps(payload, ensure_ascii=False, sort_keys=True).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def log_message(self, format: str, *args: Any) -> None:
            sys.stderr.write(f"{self.address_string()} - {format % args}\n")

    return OtaAdminHandler

def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run the 8AX VPS OTA admin API.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--storage-root", default=str(DEFAULT_STORAGE_ROOT))
    parser.add_argument("--static-root", default=str(DEFAULT_STATIC_ROOT))
    parser.add_argument("--private-root", default=str(DEFAULT_PRIVATE_ROOT))
    parser.add_argument("--drive-storage-root", default=str(DEFAULT_DRIVE_STORAGE_ROOT))
    parser.add_argument("--drive-static-root", default=str(DEFAULT_DRIVE_STATIC_ROOT))
    parser.add_argument("--drive-legacy-static-root", default=str(DEFAULT_DRIVE_LEGACY_STATIC_ROOT))
    return parser.parse_args(argv)

def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    config = load_config(args)
    server = ThreadingHTTPServer((args.host, args.port), make_handler(config))
    print(f"8AX OTA admin API listening on http://{args.host}:{args.port}")
    server.serve_forever()
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
