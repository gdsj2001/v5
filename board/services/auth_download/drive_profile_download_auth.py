from __future__ import annotations

import argparse
import json
import os
import subprocess
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, Optional, Tuple
import device_dna_register_auth as dna_auth
import v5_device_dna_register as dna_support
from device_vps_identity import extract_vps_distribution_id, require_registered_identity, VpsIdentityError
from drive_profile_download_core import b64url_encode, canonical_json_bytes, load_json_bytes, now_utc
from drive_profile_download_errors import DownloadError

DEVICE_NOT_REGISTERED_CODE = "device_not_factory_registered"
DEVICE_AUTHORIZATION_MISSING_CODE = "device_authorization_missing"
DEVICE_AUTHORIZATION_INVALID_CODE = "device_authorization_invalid"
DEVICE_PUBLIC_KEY_MISSING_CODE = "device_public_key_missing"
DEVICE_REQUEST_SIGNATURE_INVALID_CODE = "device_request_signature_invalid"
DEVICE_REQUEST_SIGNATURE_SCHEMA = "8ax-device-request-signature-v1"
DEVICE_REQUEST_SIGNATURE_ALG = "RSASSA-PKCS1-v1_5-SHA256"
DEVICE_GATE_CODES = {
    DEVICE_NOT_REGISTERED_CODE,
    DEVICE_AUTHORIZATION_MISSING_CODE,
    DEVICE_AUTHORIZATION_INVALID_CODE,
    DEVICE_PUBLIC_KEY_MISSING_CODE,
    DEVICE_REQUEST_SIGNATURE_INVALID_CODE,
    "device_request_signature_missing",
    "device_challenge_invalid",
}
DEVICE_GATE_MESSAGES = {
    DEVICE_NOT_REGISTERED_CODE: "设备未登记：请先登记本机 DNA，再执行服务器下载。",
    DEVICE_AUTHORIZATION_MISSING_CODE: "设备授权文件缺失：请先登记本机 DNA，由 VPS 签发授权文件后再服务器下载。",
    DEVICE_AUTHORIZATION_INVALID_CODE: "设备授权文件验签失败：请重新登记本机 DNA 获取 VPS 授权文件。",
    DEVICE_PUBLIC_KEY_MISSING_CODE: "设备公钥未登记：请重新登记本机 DNA。",
    DEVICE_REQUEST_SIGNATURE_INVALID_CODE: "设备请求签名校验失败：请重新登记本机 DNA 或检查设备私钥。",
    "device_request_signature_missing": "设备请求签名缺失：请重新登记本机 DNA。",
    "device_challenge_invalid": "VPS 设备挑战无效：请重新登记本机 DNA 后再试。",
}


class DeviceGateError(DownloadError):
    def __init__(self, error_code: str, message: str):
        super().__init__(message)
        self.error_code = error_code
        self.message = message

def resolve_board_dna(args: argparse.Namespace) -> Dict[str, Any]:
    override = str(args.device_dna or "").strip()
    if override:
        if os.environ.get("RE_V5_ALLOW_DEVICE_DNA_OVERRIDE", "0") == "1":
            return {
                "value": override,
                "source": "manual_override",
                "type": "zynq7000_pl_device_dna_57",
            }
        raise DownloadError("device DNA override is disabled; read live hardware DNA")
    return dna_support.read_live_dna()

def dna_hash_hex(dna: str) -> str:
    return dna_support.dna_hash_hex(dna)

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

def compact_device_authorization(envelope: Dict[str, Any]) -> str:
    return json.dumps(envelope, ensure_ascii=False, sort_keys=True, separators=(",", ":"))

def auth_error(code: str, detail_message: str = "") -> DeviceGateError:
    mapped = {
        "DEVICE_AUTH_MISSING": DEVICE_AUTHORIZATION_MISSING_CODE,
        "DEVICE_AUTH_INVALID": DEVICE_AUTHORIZATION_INVALID_CODE,
        "DEVICE_AUTH_DNA_MISMATCH": DEVICE_AUTHORIZATION_INVALID_CODE,
        "DEVICE_AUTH_KEY_MISMATCH": DEVICE_AUTHORIZATION_INVALID_CODE,
        "DEVICE_AUTH_PERMISSION_MISSING": DEVICE_AUTHORIZATION_INVALID_CODE,
        "DEVICE_AUTH_SIGNATURE_INVALID": DEVICE_AUTHORIZATION_INVALID_CODE,
        "DEVICE_AUTH_TIME_INVALID": DEVICE_AUTHORIZATION_INVALID_CODE,
        "DEVICE_AUTH_EXPIRED": DEVICE_AUTHORIZATION_INVALID_CODE,
    }.get(code, DEVICE_AUTHORIZATION_INVALID_CODE)
    return DeviceGateError(mapped, DEVICE_GATE_MESSAGES.get(mapped, detail_message or DEVICE_GATE_MESSAGES[DEVICE_AUTHORIZATION_INVALID_CODE]))

def read_verified_device_authorization(args: argparse.Namespace, dna: str) -> Tuple[str, Dict[str, Any]]:
    path = Path(args.device_auth_file)
    if not path.is_file():
        raise DeviceGateError(DEVICE_AUTHORIZATION_MISSING_CODE, DEVICE_GATE_MESSAGES[DEVICE_AUTHORIZATION_MISSING_CODE])
    try:
        key_info = dna_support.prepare_device_keypair(args, create=False)
        envelope = load_json_bytes(path.read_bytes(), str(path))
        payload = dna_support.verify_device_authorization(
            envelope,
            dna,
            args.device_auth_public_key_file,
            key_info["public_key_sha256"],
        )
    except dna_support.DnaRegisterError as exc:
        raise auth_error(exc.code, exc.message)
    except Exception as exc:
        raise DeviceGateError(DEVICE_AUTHORIZATION_INVALID_CODE, "%s: %s" % (DEVICE_GATE_MESSAGES[DEVICE_AUTHORIZATION_INVALID_CODE], exc))
    return compact_device_authorization(envelope), payload

def request_path_for_url(url: str) -> str:
    parsed = urllib.parse.urlsplit(url)
    path = parsed.path or "/"
    if parsed.query:
        path += "?" + parsed.query
    return path

def base_url_for_url(url: str) -> str:
    parsed = urllib.parse.urlsplit(url)
    return "%s://%s" % (parsed.scheme, parsed.netloc)


def resolve_challenge_base_url(args: argparse.Namespace, download_url: str) -> str:
    configured = str(getattr(args, "server_url", "") or "").strip()
    if configured:
        return configured.split(",")[0].strip().rstrip("/")
    config_path = Path(str(getattr(args, "vps_endpoints_config", "") or ""))
    try:
        payload = json.loads(config_path.read_text(encoding="utf-8-sig"))
        urls = payload.get("api_base_urls") or payload.get("api_urls") or payload.get("server_urls") or []
        if isinstance(urls, str):
            urls = [urls]
        for value in urls:
            text = str(value or "").strip().rstrip("/")
            if text.startswith("http://") or text.startswith("https://"):
                return text
    except Exception:
        pass
    return base_url_for_url(download_url)

def openssl_sign_private(private_key_file: str, payload_bytes: bytes) -> bytes:
    result = subprocess.run(
        [dna_auth.openssl_path(), "dgst", "-sha256", "-sign", str(private_key_file)],
        input=payload_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10.0,
    )
    if result.returncode != 0 or not result.stdout:
        raise DeviceGateError(DEVICE_REQUEST_SIGNATURE_INVALID_CODE, DEVICE_GATE_MESSAGES[DEVICE_REQUEST_SIGNATURE_INVALID_CODE])
    return result.stdout

def http_post_json(url: str, timeout: float, payload: Dict[str, Any]) -> Dict[str, Any]:
    data = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=data,
        headers={
            "Accept": "application/json",
            "Content-Type": "application/json; charset=utf-8",
            "User-Agent": "re-v5-drive-profile-download/1",
        },
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return load_json_bytes(resp.read(), url)

def describe_http_error(exc: urllib.error.HTTPError) -> Tuple[str, str, str]:
    try:
        body = exc.read().decode("utf-8", errors="replace")[:1200]
    except Exception:
        body = ""
    status = ""
    message = ""
    if body:
        try:
            payload = json.loads(body)
            if isinstance(payload, dict):
                status = str(payload.get("status") or payload.get("error") or payload.get("code") or "").strip()
                message = str(payload.get("message") or "").strip()
        except Exception:
            pass
    text = ("%s %s %s" % (status, message, body)).lower()
    for code in DEVICE_GATE_CODES:
        if code in text or status == code:
            return code, DEVICE_GATE_MESSAGES.get(code, message), "HTTP%s:%s:%s" % (exc.code, code, message)
    if exc.code == 403 and ("dna" in text or "authorization" in text):
        return DEVICE_AUTHORIZATION_INVALID_CODE, DEVICE_GATE_MESSAGES[DEVICE_AUTHORIZATION_INVALID_CODE], "HTTP403:device-authorization-gate:%s" % message
    detail = "HTTP%s" % exc.code
    if status:
        detail += ":%s" % status
    if message:
        detail += ":%s" % message
    return "", message, detail

def request_device_challenge(server_url: str, timeout: float, device_id: str, dna_hash: str) -> Dict[str, Any]:
    url = "%s/api/v1/device/challenge" % server_url.rstrip("/")
    payload = {
        "device_id": device_id,
        "vps_distribution_id": device_id,
        "vpsDistributionId": device_id,
        "purpose": "drive_profile_download",
        "pl_device_dna_hash": dna_hash,
        "pl_dna_hash": dna_hash,
        "client_time": now_utc(),
    }
    try:
        response = http_post_json(url, timeout, payload)
    except urllib.error.HTTPError as exc:
        error_code, message, _detail = describe_http_error(exc)
        raise DeviceGateError(error_code or DEVICE_REQUEST_SIGNATURE_INVALID_CODE, message or DEVICE_GATE_MESSAGES[DEVICE_REQUEST_SIGNATURE_INVALID_CODE])
    if not bool(response.get("ok", response.get("success", False))):
        code = str(response.get("status") or response.get("error") or DEVICE_REQUEST_SIGNATURE_INVALID_CODE)
        raise DeviceGateError(code, str(response.get("message") or DEVICE_GATE_MESSAGES.get(code, DEVICE_GATE_MESSAGES[DEVICE_REQUEST_SIGNATURE_INVALID_CODE])))
    nonce = str(response.get("nonce") or "").strip()
    if not nonce:
        raise DeviceGateError("device_challenge_invalid", DEVICE_GATE_MESSAGES["device_challenge_invalid"])
    return response

class DeviceRequestSigner:
    def __init__(self, args: argparse.Namespace, dna: str, authorization_payload: Dict[str, Any]):
        self.args = args
        self.dna = dna
        self.dna_hash = dna_hash_hex(dna)
        try:
            self.key_info = dna_support.prepare_device_keypair(args, create=False)
        except dna_support.DnaRegisterError as exc:
            raise DeviceGateError(DEVICE_REQUEST_SIGNATURE_INVALID_CODE, exc.message)
        try:
            self.identity = require_registered_identity(args.register_status_path, self.dna_hash)
        except VpsIdentityError as exc:
            raise DeviceGateError(DEVICE_NOT_REGISTERED_CODE, exc.message)
        self.device_id = self.identity["vps_distribution_id"]
        auth_device_id = extract_vps_distribution_id(authorization_payload)
        if auth_device_id and auth_device_id != self.device_id:
            raise DeviceGateError(DEVICE_AUTHORIZATION_INVALID_CODE, DEVICE_GATE_MESSAGES[DEVICE_AUTHORIZATION_INVALID_CODE])
        self.device_public_key_sha256 = str(authorization_payload.get("device_public_key_sha256") or "").strip().lower()
        if not self.device_id or not self.device_public_key_sha256:
            raise DeviceGateError(DEVICE_AUTHORIZATION_INVALID_CODE, DEVICE_GATE_MESSAGES[DEVICE_AUTHORIZATION_INVALID_CODE])
        if self.device_public_key_sha256 != self.key_info["public_key_sha256"].lower():
            raise DeviceGateError(DEVICE_AUTHORIZATION_INVALID_CODE, DEVICE_GATE_MESSAGES[DEVICE_AUTHORIZATION_INVALID_CODE])

    def headers_for_url(self, url: str, method: str = "GET") -> Dict[str, str]:
        request_path = request_path_for_url(url)
        challenge = request_device_challenge(resolve_challenge_base_url(self.args, url), self.args.timeout, self.device_id, self.dna_hash)
        nonce = str(challenge.get("nonce") or "").strip()
        timestamp = str(challenge.get("time") or "").strip() or now_utc()
        payload_data = {
            "schema": DEVICE_REQUEST_SIGNATURE_SCHEMA,
            "alg": DEVICE_REQUEST_SIGNATURE_ALG,
            "device_id": self.device_id,
            "nonce": nonce,
            "pl_device_dna_hash": self.dna_hash,
            "purpose": "drive_profile_download",
            "request_method": method.upper(),
            "request_path": request_path,
            "timestamp": timestamp,
        }
        signature = openssl_sign_private(self.key_info["private_key_file"], canonical_json_bytes(payload_data))
        return {
            "X-8AX-Device-ID": self.device_id,
            "X-8AX-VPS-Distribution-ID": self.device_id,
            "X-8AX-PL-DNA-Hash": self.dna_hash,
            "X-8AX-Device-Challenge": nonce,
            "X-8AX-Device-Timestamp": timestamp,
            "X-8AX-Device-Request-Signature": b64url_encode(signature),
            "X-8AX-Device-Request-Signature-Alg": DEVICE_REQUEST_SIGNATURE_ALG,
            "X-8AX-Device-Public-Key-SHA256": self.device_public_key_sha256,
        }

