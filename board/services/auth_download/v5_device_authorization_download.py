#!/usr/bin/env python3
"""Download the factory-signed device authorization envelope for this board."""

from __future__ import annotations

import argparse
import json
import os
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Dict, List, Optional

from device_auth_latch import DEFAULT_AUTH_LATCH_STATUS_PATH, write_device_auth_latch
from v5_device_dna_register import (
    DEFAULT_DEVICE_AUTH_FILE,
    DEFAULT_DEVICE_AUTH_PUBLIC_KEY_FILE,
    DEFAULT_DEVICE_PRIVATE_KEY_FILE,
    DEFAULT_DEVICE_PUBLIC_KEY_FILE,
    DEFAULT_REGISTER_STATUS_PATH,
    DEFAULT_VPS_ENDPOINTS_CONFIG,
    DnaRegisterError,
    atomic_write_json,
    device_authorization_signature_hash,
    dna_hash_hex,
    job_token_from_context,
    prepare_device_keypair,
    read_live_dna,
    raise_if_cancelled,
    redact_text,
    resolve_api_base_urls,
    verify_device_authorization,
)
from device_vps_identity import extract_vps_distribution_id, require_registered_identity, scrub_local_dna_fields, VpsIdentityError


DEFAULT_RESULT_PATH = "/run/8ax_v5_auth_download/device_authorization_download_result.json"


def local_result(payload: Dict[str, Any]) -> Dict[str, Any]:
    cleaned = scrub_local_dna_fields(payload)
    return cleaned if isinstance(cleaned, dict) else dict(payload)


def now_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def load_json_bytes(data: bytes, source: str) -> Dict[str, Any]:
    try:
        value = json.loads(data.decode("utf-8"))
    except Exception as exc:
        raise DnaRegisterError("DEVICE_AUTH_DOWNLOAD_BAD_JSON", f"授权下载返回不是有效 JSON: {source}") from exc
    if not isinstance(value, dict):
        raise DnaRegisterError("DEVICE_AUTH_DOWNLOAD_BAD_JSON", "授权下载返回不是 JSON object")
    return value


def http_get_json(url: str, timeout: float, dna: str, identity: Dict[str, str]) -> Dict[str, Any]:
    headers = {
        "Accept": "application/json",
        "User-Agent": "re-v5-device-authorization-download/1",
        "X-8AX-Device-DNA": dna,
        "X-8AX-License-Anchor": dna,
        "X-8AX-Device-ID": identity["vps_distribution_id"],
        "X-8AX-VPS-Distribution-ID": identity["vps_distribution_id"],
        "X-8AX-PL-DNA-Hash": identity["pl_dna_hash"],
    }
    req = urllib.request.Request(url, headers=headers, method="GET")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return load_json_bytes(resp.read(), url)


def extract_envelope(response: Dict[str, Any]) -> Dict[str, Any]:
    envelope = response.get("deviceAuthorization") or response.get("device_authorization")
    if not isinstance(envelope, dict):
        raise DnaRegisterError("DEVICE_AUTH_DOWNLOAD_MISSING", "VPS 未返回设备授权文件")
    return envelope


def run(args: argparse.Namespace, job_token: Any = None) -> Dict[str, Any]:
    raise_if_cancelled(job_token)
    dna_report = read_live_dna()
    dna = str(dna_report["value"]).upper()
    dna_hash = dna_hash_hex(dna)
    key_info = prepare_device_keypair(args, create=False)
    server_urls = resolve_api_base_urls(args)
    try:
        identity = require_registered_identity(args.register_status_path, dna_hash)
    except VpsIdentityError as exc:
        result = {
            "ok": False,
            "code": exc.code,
            "schema": "re-v5-device-authorization-download-v1",
            "generated_at": now_utc(),
            "message_cn": exc.message,
            "server_attempted": False,
            "license_anchor": {
                "source": dna_report.get("source", ""),
                "type": dna_report.get("type", ""),
                "value_stored_locally": False,
                "hash": dna_hash,
                "hash_short": dna_hash[:8],
            },
        }
        public_result = local_result(result)
        atomic_write_json(Path(args.result_path), public_result)
        return public_result
    errors: List[str] = []
    for server_url in server_urls:
        raise_if_cancelled(job_token)
        url = "%s/api/v1/device/authorization" % server_url.rstrip("/")
        try:
            response = http_get_json(url, args.timeout, dna, identity)
            raise_if_cancelled(job_token)
            envelope = extract_envelope(response)
            payload = verify_device_authorization(
                envelope,
                dna,
                args.device_auth_public_key_file,
                key_info["public_key_sha256"],
            )
            returned_id = extract_vps_distribution_id(payload) or extract_vps_distribution_id(response) or identity["vps_distribution_id"]
            if returned_id != identity["vps_distribution_id"]:
                raise DnaRegisterError("DEVICE_AUTH_DOWNLOAD_ID_MISMATCH", "授权文件 VPS 分发ID与本机登记状态不匹配")
            auth_path = Path(args.device_auth_file)
            atomic_write_json(auth_path, envelope)
            try:
                os.chmod(str(auth_path), 0o600)
            except Exception:
                pass
            result = {
                "ok": True,
                "code": "DEVICE_AUTH_DOWNLOAD_OK",
                "schema": "re-v5-device-authorization-download-v1",
                "generated_at": now_utc(),
                "message_cn": "授权文件已下载并通过本机公钥验签",
                "server_url": server_url,
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
                "device_authorization": {
                    "stored": True,
                    "path": str(auth_path),
                    "schema": envelope.get("schema", ""),
                    "key_id": payload.get("key_id", ""),
                    "device_id": identity["vps_distribution_id"],
                    "vps_distribution_id": identity["vps_distribution_id"],
                    "pl_dna_hash": payload.get("pl_device_dna_hash", ""),
                    "device_public_key_sha256": payload.get("device_public_key_sha256", ""),
                    "signature_hash": device_authorization_signature_hash(envelope),
                    "not_before": payload.get("not_before", ""),
                    "expires_at": payload.get("expires_at", ""),
                    "permissions": payload.get("permissions", []) if isinstance(payload.get("permissions"), list) else [],
                },
            }
            write_device_auth_latch(result, args.auth_latch_status_path)
            public_result = local_result(result)
            atomic_write_json(Path(args.result_path), public_result)
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

    result = {
        "ok": False,
        "code": "DEVICE_AUTH_DOWNLOAD_FAILED",
        "schema": "re-v5-device-authorization-download-v1",
        "generated_at": now_utc(),
        "message_cn": "授权文件下载失败：请先用工厂客户端生成并上传授权",
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
    }
    public_result = local_result(result)
    atomic_write_json(Path(args.result_path), public_result)
    return public_result


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Download factory-signed 8AX device authorization.")
    parser.add_argument("--vps-endpoints-config", default=os.environ.get("RE_V5_VPS_ENDPOINTS_CONFIG", DEFAULT_VPS_ENDPOINTS_CONFIG))
    parser.add_argument("--server-url", default="")
    parser.add_argument("--result-path", default=os.environ.get("RE_V5_DEVICE_AUTH_DOWNLOAD_RESULT_PATH", DEFAULT_RESULT_PATH))
    parser.add_argument("--register-status-path", default=os.environ.get("RE_V5_DEVICE_DNA_REGISTER_STATUS_PATH", DEFAULT_REGISTER_STATUS_PATH))
    parser.add_argument("--auth-latch-status-path", default=os.environ.get("RE_V5_DEVICE_AUTH_LATCH_STATUS", DEFAULT_AUTH_LATCH_STATUS_PATH))
    parser.add_argument("--device-auth-file", default=os.environ.get("RE_V5_DEVICE_AUTH_FILE", DEFAULT_DEVICE_AUTH_FILE))
    parser.add_argument("--device-auth-public-key-file", default=os.environ.get("RE_V5_DEVICE_AUTH_PUBLIC_KEY_FILE", DEFAULT_DEVICE_AUTH_PUBLIC_KEY_FILE))
    parser.add_argument("--device-private-key-file", default=os.environ.get("RE_V5_DEVICE_PRIVATE_KEY_FILE", DEFAULT_DEVICE_PRIVATE_KEY_FILE))
    parser.add_argument("--device-public-key-file", default=os.environ.get("RE_V5_DEVICE_PUBLIC_KEY_FILE", DEFAULT_DEVICE_PUBLIC_KEY_FILE))
    parser.add_argument("--timeout", type=float, default=float(os.environ.get("RE_V5_DEVICE_AUTH_DOWNLOAD_TIMEOUT", "10")))
    parser.add_argument("--request", default="")
    return parser.parse_args(argv)


def run_action(request: Dict[str, Any], context: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    del request
    job_token = job_token_from_context(context)
    raise_if_cancelled(job_token)
    return run(parse_args([]), job_token=job_token)


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    result = run(args)
    print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
    return 0 if result.get("ok") else 2


if __name__ == "__main__":
    raise SystemExit(main())

