from __future__ import annotations

import argparse
import os
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple
import v5_device_dna_register as dna_support
from drive_profile_download_auth import DEVICE_GATE_CODES, DEVICE_GATE_MESSAGES, DeviceGateError, DeviceRequestSigner, describe_http_error
from drive_profile_download_core import atomic_write, b64url_encode, parse_sha256_sidecar, sha256_bytes
from drive_profile_download_errors import DownloadError
from drive_profile_download_install import install_payload, sha256_file

def raise_if_cancelled(job_token: Any) -> None:
    dna_support.raise_if_cancelled(job_token)

def http_get(
    url: str,
    token: str,
    timeout: float,
    device_dna: str = "",
    device_authorization: str = "",
    device_request_headers: Optional[Dict[str, str]] = None,
) -> Tuple[bytes, str]:
    headers = {
        "Accept": "application/json, application/zip, application/octet-stream, */*",
        "User-Agent": "re-v5-drive-profile-download/1",
    }
    if token:
        headers["Authorization"] = "Bearer " + token
    if device_dna:
        headers["X-8AX-Device-DNA"] = device_dna
        headers["X-8AX-License-Anchor"] = device_dna
    if device_authorization:
        headers["X-8AX-Device-Authorization"] = b64url_encode(device_authorization.encode("utf-8"))
    if device_request_headers:
        headers.update({str(k): str(v) for k, v in device_request_headers.items() if v is not None})
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read(), resp.headers.get("Content-Type", "")

def first_success(
    urls: Iterable[Tuple[str, str]],
    token: str,
    timeout: float,
    out_dir: Path,
    device_dna_header: str,
    device_authorization: str,
    request_signer: Optional[DeviceRequestSigner],
    job_token: Any = None,
) -> Dict[str, Any]:
    errors: List[str] = []
    for url, artifact_name in urls:
        raise_if_cancelled(job_token)
        try:
            sha256_url = ""
            remote_sha256 = ""
            if artifact_name == "driver_profile_map.json":
                sha256_url = url + ".sha256"
                signed_sha_headers = request_signer.headers_for_url(sha256_url) if request_signer else None
                try:
                    raise_if_cancelled(job_token)
                    sha_data, _sha_content_type = http_get(
                        sha256_url,
                        token,
                        timeout,
                        device_dna_header,
                        device_authorization,
                        signed_sha_headers,
                    )
                    raise_if_cancelled(job_token)
                except Exception as exc:
                    raise DownloadError("%s SHA256 sidecar 下载失败: %s" % (sha256_url, exc)) from exc
                remote_sha256 = parse_sha256_sidecar(sha_data, sha256_url)
            signed_headers = request_signer.headers_for_url(url) if request_signer else None
            raise_if_cancelled(job_token)
            data, content_type = http_get(url, token, timeout, device_dna_header, device_authorization, signed_headers)
            raise_if_cancelled(job_token)
            download_sha256 = sha256_bytes(data)
            if remote_sha256 and download_sha256.upper() != remote_sha256.upper():
                raise DownloadError(
                    "driver_profile_map.json 下载 SHA 与 VPS 声明不一致: %s != %s" % (download_sha256, remote_sha256)
                )
            installed = install_payload(data, content_type, out_dir, artifact_name)
            target = out_dir / artifact_name
            installed_sha256 = sha256_file(target) if target.is_file() else ""
            byte_preserved = bool(installed_sha256 and installed_sha256.upper() == download_sha256.upper())
            if artifact_name == "driver_profile_map.json" and not byte_preserved:
                raise DownloadError("driver_profile_map.json 下载后 SHA 不一致，拒绝标记成功。")
            if artifact_name == "driver_profile_map.json" and remote_sha256:
                atomic_write(out_dir / (artifact_name + ".sha256"), f"{remote_sha256}  {artifact_name}\n".encode("ascii"))
            return {
                "ok": True,
                "url": url,
                "sha256_url": sha256_url,
                "installed": installed,
                "content_type": content_type,
                "remote_sha256": remote_sha256,
                "download_sha256": download_sha256,
                "installed_sha256": installed_sha256,
                "byte_preserved": byte_preserved,
                "vps_sha256_verified": bool(remote_sha256 and installed_sha256.upper() == remote_sha256.upper()),
            }
        except urllib.error.HTTPError as exc:
            error_code, message, detail = describe_http_error(exc)
            errors.append("%s:%s" % (url, redact_sensitive_download_text(detail, device_dna_header)))
            if error_code in DEVICE_GATE_CODES:
                return {"ok": False, "url": url, "error_code": error_code, "message": message, "errors": errors[-6:]}
        except DeviceGateError as exc:
            errors.append("%s:%s:%s" % (url, exc.error_code, exc.message))
            if exc.error_code in DEVICE_GATE_CODES:
                return {"ok": False, "url": url, "error_code": exc.error_code, "message": exc.message, "errors": errors[-6:]}
        except Exception as exc:
            errors.append("%s:%s" % (url, redact_sensitive_download_text(exc, device_dna_header)))
    return {"ok": False, "errors": errors[-6:]}

def redact_sensitive_download_text(value: Any, dna: str) -> Any:
    if not isinstance(value, str):
        value = str(value)
    text = value
    if dna:
        text = text.replace(urllib.parse.quote(dna, safe=""), "<redacted>")
        text = text.replace(dna, "<redacted>")
        text = text.replace(dna.upper(), "<redacted>")
        text = text.replace(dna.lower(), "<redacted>")
    return text

def redact_download_result(result: Dict[str, Any], dna: str) -> Dict[str, Any]:
    redacted: Dict[str, Any] = {}
    for key, value in result.items():
        if isinstance(value, list):
            redacted[key] = [redact_sensitive_download_text(item, dna) for item in value]
        else:
            redacted[key] = redact_sensitive_download_text(value, dna) if isinstance(value, str) else value
    return redacted

def download_result_indicates_absent(result: Dict[str, Any]) -> bool:
    haystack = " ".join(
        str(item)
        for item in (
            result.get("error_code", ""),
            result.get("message", ""),
            result.get("error", ""),
            result.get("reason", ""),
            result.get("errors", []),
        )
    ).lower()
    return "private_absent" in haystack or "http404" in haystack or "404" in haystack

def device_gate_error_result(*results: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    for result in results:
        if isinstance(result, dict) and result.get("error_code") in DEVICE_GATE_CODES:
            return result
    return None

def build_download_urls(
    server_urls: List[str],
    static_urls: List[str],
    scope: str,
    dna_hash: str = "",
    vps_distribution_id: str = "",
) -> List[Tuple[str, str]]:
    del static_urls, dna_hash
    urls: List[Tuple[str, str]] = []
    for server_url in server_urls:
        base = str(server_url or "").rstrip("/")
        if not base:
            continue
        if scope == "private":
            if not vps_distribution_id:
                continue
            safe_id = urllib.parse.quote(str(vps_distribution_id), safe="")
            urls.append(("%s/api/v1/drive/profiles/private/%s/driver_profile_map.json" % (base, safe_id), "driver_profile_map.json"))
        else:
            urls.append(("%s/api/v1/drive/profiles/%s/driver_profile_map.json" % (base, scope), "driver_profile_map.json"))
    return urls

def should_probe_private(args: argparse.Namespace, token: str) -> Tuple[bool, str]:
    mode = str(args.private_mode or "auto").strip().lower()
    legacy = os.environ.get("RE_V5_DRIVE_PROFILE_PRIVATE", os.environ.get("AX8_DRIVE_PROFILE_PRIVATE", "")).strip().lower()
    if legacy in ("1", "true", "yes", "on", "always"):
        mode = "always"
    elif legacy in ("0", "false", "no", "off", "skip"):
        mode = "skip"
    if mode == "always":
        return True, "forced"
    if mode == "skip":
        return False, "disabled"
    return True, "device_authorized"

