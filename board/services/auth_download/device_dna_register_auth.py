from __future__ import annotations

import base64
import hashlib
import json
import os
import subprocess
import tempfile
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Dict, Optional
DEFAULT_DEVICE_AUTH_PUBLIC_KEY_FILE = '/etc/6x-cnc/device_auth_public.pem'
DEVICE_AUTH_SCHEMA = '8ax-device-authorization-v1'
DEVICE_AUTH_ENVELOPE_SCHEMA = '8ax-device-authorization-envelope-v1'
DEVICE_AUTH_SIGNATURE_ALG = 'RSASSA-PKCS1-v1_5-SHA256'

class DnaRegisterError(RuntimeError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code
        self.message = message

def now_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

def load_json_bytes(data: bytes, source: str) -> Dict[str, Any]:
    try:
        value = json.loads(data.decode("utf-8-sig"))
    except Exception as exc:
        raise DnaRegisterError("DNA_REGISTER_JSON_INVALID", "%s 不是有效 JSON: %s" % (source, exc))
    if not isinstance(value, dict):
        raise DnaRegisterError("DNA_REGISTER_JSON_INVALID", "%s JSON 根节点不是对象" % source)
    return value

def load_json(path: str | os.PathLike[str]) -> Dict[str, Any]:
    target = Path(path)
    try:
        data = target.read_bytes()
    except Exception as exc:
        raise DnaRegisterError("DNA_REGISTER_JSON_READ_FAILED", "%s read failed: %s" % (target, exc))
    return load_json_bytes(data, str(target))

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

def dna_hash_hex(dna: str) -> str:
    return hashlib.sha256(str(dna or "").strip().upper().encode("ascii", errors="ignore")).hexdigest()

def b64url_decode(text: str) -> bytes:
    raw = str(text or "").strip()
    raw += "=" * ((4 - len(raw) % 4) % 4)
    return base64.urlsafe_b64decode(raw.encode("ascii"))

def canonical_json_bytes(data: Dict[str, Any]) -> bytes:
    return json.dumps(data, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")

def parse_iso_utc(value: Any) -> Optional[datetime]:
    text = str(value or "").strip()
    if not text:
        return None
    if text.endswith("Z"):
        text = text[:-1] + "+00:00"
    try:
        parsed = datetime.fromisoformat(text)
    except Exception:
        return None
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(timezone.utc)

def openssl_path() -> str:
    return os.environ.get("RE_V5_OPENSSL", "openssl")

def normalize_public_key_pem(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n").strip() + b"\n"

def public_key_sha256_from_bytes(data: bytes) -> str:
    return hashlib.sha256(normalize_public_key_pem(data)).hexdigest()

def set_private_file_permissions(path: Path) -> None:
    try:
        os.chmod(str(path), 0o600)
    except Exception:
        pass
    try:
        if hasattr(os, "chown"):
            os.chown(str(path), 0, 0)
    except Exception:
        pass

def prepare_device_keypair(args: argparse.Namespace, create: bool) -> Dict[str, str]:
    private_key = Path(args.device_private_key_file)
    public_key = Path(args.device_public_key_file)
    private_key.parent.mkdir(parents=True, exist_ok=True)
    if not private_key.is_file():
        if not create:
            raise DnaRegisterError("DEVICE_PRIVATE_KEY_MISSING", "设备私钥缺失，请重新登记本机 DNA")
        old_umask = os.umask(0o177)
        try:
            proc = subprocess.run(
                [openssl_path(), "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:3072", "-out", str(private_key)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=30.0,
            )
        finally:
            os.umask(old_umask)
        if proc.returncode != 0 or not private_key.is_file():
            raise DnaRegisterError("DEVICE_PRIVATE_KEY_CREATE_FAILED", "设备私钥生成失败")
    set_private_file_permissions(private_key)
    proc = subprocess.run(
        [openssl_path(), "rsa", "-pubout", "-in", str(private_key)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10.0,
    )
    if proc.returncode != 0 or not proc.stdout:
        raise DnaRegisterError("DEVICE_PUBLIC_KEY_EXPORT_FAILED", "设备公钥导出失败")
    public_pem = normalize_public_key_pem(proc.stdout)
    atomic_write(public_key, public_pem)
    try:
        os.chmod(str(public_key), 0o644)
    except Exception:
        pass
    return {
        "private_key_file": str(private_key),
        "public_key_file": str(public_key),
        "public_key_pem": public_pem.decode("ascii", errors="ignore"),
        "public_key_sha256": public_key_sha256_from_bytes(public_pem),
    }

def openssl_verify(public_key_file: str, payload_bytes: bytes, signature_bytes: bytes) -> bool:
    public_key_path = Path(public_key_file)
    if not public_key_path.is_file():
        raise DnaRegisterError("DEVICE_AUTH_PUBLIC_KEY_MISSING", "设备授权验签公钥缺失")
    tmp_name = ""
    try:
        with tempfile.NamedTemporaryFile(prefix="re-v5-device-auth-sig.", delete=False) as fh:
            fh.write(signature_bytes)
            tmp_name = fh.name
        proc = subprocess.run(
            [openssl_path(), "dgst", "-sha256", "-verify", str(public_key_path), "-signature", tmp_name],
            input=payload_bytes,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10.0,
        )
        return proc.returncode == 0
    finally:
        if tmp_name:
            try:
                os.unlink(tmp_name)
            except FileNotFoundError:
                pass

def verify_device_authorization(
    envelope: Dict[str, Any],
    dna: str,
    public_key_file: str,
    device_public_key_sha256: str = "",
) -> Dict[str, Any]:
    if not isinstance(envelope, dict) or envelope.get("schema") != DEVICE_AUTH_ENVELOPE_SCHEMA:
        raise DnaRegisterError("DEVICE_AUTH_INVALID", "设备授权文件 schema 无效")
    payload = envelope.get("payload")
    signature = envelope.get("signature")
    if not isinstance(payload, dict) or not isinstance(signature, dict):
        raise DnaRegisterError("DEVICE_AUTH_INVALID", "设备授权文件结构无效")
    if payload.get("schema") != DEVICE_AUTH_SCHEMA:
        raise DnaRegisterError("DEVICE_AUTH_INVALID", "设备授权 payload schema 无效")
    if payload.get("signature_alg") != DEVICE_AUTH_SIGNATURE_ALG or signature.get("alg") != DEVICE_AUTH_SIGNATURE_ALG:
        raise DnaRegisterError("DEVICE_AUTH_INVALID", "设备授权签名算法无效")
    if str(payload.get("key_id", "")) != str(signature.get("key_id", "")):
        raise DnaRegisterError("DEVICE_AUTH_INVALID", "设备授权 key_id 不一致")
    if str(payload.get("pl_device_dna_hash", "")).lower() != dna_hash_hex(dna).lower():
        raise DnaRegisterError("DEVICE_AUTH_DNA_MISMATCH", "设备授权 DNA 摘要与本机不匹配")
    expected_key_sha = str(device_public_key_sha256 or "").strip().lower()
    actual_key_sha = str(payload.get("device_public_key_sha256", "")).strip().lower()
    if expected_key_sha and actual_key_sha != expected_key_sha:
        raise DnaRegisterError("DEVICE_AUTH_KEY_MISMATCH", "设备授权公钥指纹与本机不匹配")
    permissions = [str(item) for item in payload.get("permissions", [])] if isinstance(payload.get("permissions"), list) else []
    if "drive_profile_download" not in permissions:
        raise DnaRegisterError("DEVICE_AUTH_PERMISSION_MISSING", "设备授权缺少 drive_profile_download 权限")
    signature_bytes = b64url_decode(str(signature.get("value", "")))
    if not signature_bytes or not openssl_verify(public_key_file, canonical_json_bytes(payload), signature_bytes):
        raise DnaRegisterError("DEVICE_AUTH_SIGNATURE_INVALID", "设备授权验签失败")
    now = datetime.now(timezone.utc).replace(microsecond=0)
    not_before = parse_iso_utc(payload.get("not_before"))
    expires_at = parse_iso_utc(payload.get("expires_at"))
    if not_before is None or now + timedelta(minutes=5) < not_before:
        raise DnaRegisterError("DEVICE_AUTH_TIME_INVALID", "设备授权尚未生效")
    if expires_at is not None and now > expires_at:
        raise DnaRegisterError("DEVICE_AUTH_EXPIRED", "设备授权已过期")
    return payload

def device_authorization_signature_hash(envelope: Dict[str, Any]) -> str:
    signature = envelope.get("signature", {}) if isinstance(envelope, dict) else {}
    value = str(signature.get("value", "") if isinstance(signature, dict) else "")
    return hashlib.sha256(value.encode("ascii", errors="ignore")).hexdigest() if value else ""

def store_response_device_authorization(response: Dict[str, Any], args: argparse.Namespace, dna: str, key_info: Dict[str, str]) -> Dict[str, Any]:
    envelope = response.get("deviceAuthorization") or response.get("device_authorization")
    if not isinstance(envelope, dict):
        raise DnaRegisterError("DEVICE_AUTH_MISSING", "VPS 未返回设备授权文件，不写入本地登记状态")
    payload = verify_device_authorization(envelope, dna, args.device_auth_public_key_file, key_info["public_key_sha256"])
    auth_path = Path(args.device_auth_file)
    atomic_write_json(auth_path, envelope)
    try:
        os.chmod(str(auth_path), 0o600)
    except Exception:
        pass
    return {
        "stored": True,
        "path": str(auth_path),
        "schema": envelope.get("schema", ""),
        "key_id": payload.get("key_id", ""),
        "device_id": payload.get("device_id", ""),
        "pl_dna_hash": payload.get("pl_device_dna_hash", ""),
        "device_public_key_sha256": payload.get("device_public_key_sha256", ""),
        "signature_hash": device_authorization_signature_hash(envelope),
        "not_before": payload.get("not_before", ""),
        "expires_at": payload.get("expires_at", ""),
        "permissions": payload.get("permissions", []) if isinstance(payload.get("permissions"), list) else [],
    }

def existing_authorization_status(args: argparse.Namespace, dna: str, key_info: Dict[str, str]) -> Dict[str, Any]:
    path = Path(args.device_auth_file)
    if not path.is_file():
        raise DnaRegisterError("DEVICE_AUTH_MISSING", "本地设备授权文件缺失")
    envelope = load_json(str(path))
    payload = verify_device_authorization(envelope, dna, args.device_auth_public_key_file, key_info["public_key_sha256"])
    return {
        "stored": True,
        "path": str(path),
        "schema": envelope.get("schema", ""),
        "key_id": payload.get("key_id", ""),
        "device_id": payload.get("device_id", ""),
        "pl_dna_hash": payload.get("pl_device_dna_hash", ""),
        "device_public_key_sha256": payload.get("device_public_key_sha256", ""),
        "signature_hash": device_authorization_signature_hash(envelope),
        "not_before": payload.get("not_before", ""),
        "expires_at": payload.get("expires_at", ""),
        "permissions": payload.get("permissions", []) if isinstance(payload.get("permissions"), list) else [],
    }
