from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
from typing import Any, Dict, List, Optional
import v5_device_dna_register as dna_support
from drive_profile_download_args import parse_args
from drive_profile_download_auth import DEVICE_GATE_MESSAGES, DeviceGateError, DeviceRequestSigner, dna_hash_hex, read_verified_device_authorization, resolve_board_dna
from drive_profile_download_cache import invalidate_private_active_mapping, remove_legacy_effective_cache, remove_private_cache
from drive_profile_download_core import atomic_write_json, now_utc, read_text_token
from drive_profile_download_endpoints import resolve_api_base_urls, resolve_static_base_urls
from drive_profile_download_errors import DownloadError
from drive_profile_download_install import map_cache_status, read_map, validate_profile_map
from drive_profile_download_ipv4 import install_ipv4_urlopen_resolution
from drive_profile_download_status import current_status_epoch, read_request, request_status_epoch, stale_epoch_result
from drive_profile_download_transport import build_download_urls, device_gate_error_result, download_result_indicates_absent, first_success, raise_if_cancelled, redact_download_result, should_probe_private
from device_vps_identity import scrub_local_dna_fields

RUN_DIR = Path("/run/8ax_v5_auth_download")
REQUEST_STATUS_EPOCH: Optional[int] = None
SERVER_DOWNLOAD_PROGRESS_SCHEMA = "re.v5.drive_profile_server_download.progress.v1"
SERVER_DOWNLOAD_PROGRESS_TOTAL = 6


def attach_request_status_epoch(payload: Dict[str, Any]) -> Dict[str, Any]:
    if REQUEST_STATUS_EPOCH is None or "request_status_epoch" in payload:
        return payload
    updated = dict(payload)
    updated["request_status_epoch"] = REQUEST_STATUS_EPOCH
    return updated

def job_token_from_context(context: Optional[Dict[str, Any]]) -> Any:
    return dna_support.job_token_from_context(context)

def server_download_progress_path() -> Path:
    return RUN_DIR / "drive_profile_server_download_progress.json"

def write_server_download_progress(
    stage: str,
    step: int,
    message_cn: str,
    *,
    ok: bool = True,
    code: str = "",
    failed_stage: str = "",
    extra: Optional[Dict[str, Any]] = None,
) -> None:
    payload: Dict[str, Any] = {
        "schema": SERVER_DOWNLOAD_PROGRESS_SCHEMA,
        "action": "server-download",
        "stage": stage,
        "step": max(1, int(step)),
        "total": SERVER_DOWNLOAD_PROGRESS_TOTAL,
        "message_cn": message_cn,
        "ok": bool(ok),
        "code": code,
        "generated_at": now_utc(),
    }
    if failed_stage:
        payload["failed_stage"] = failed_stage
    if extra:
        payload.update(extra)
    try:
        atomic_write_json(server_download_progress_path(), payload)
    except Exception:
        pass

def write_result(report: Dict[str, Any]) -> None:
    try:
        report = attach_request_status_epoch(report)
        cleaned = scrub_local_dna_fields(report)
        atomic_write_json(RUN_DIR / "drive_profile_server_download_result.json", cleaned if isinstance(cleaned, dict) else report)
    except Exception:
        pass

def local_report(report: Dict[str, Any]) -> Dict[str, Any]:
    cleaned = scrub_local_dna_fields(report)
    return cleaned if isinstance(cleaned, dict) else report

def run(args: argparse.Namespace, job_token: Any = None) -> Dict[str, Any]:
    raise_if_cancelled(job_token)
    write_server_download_progress("request_check", 1, "检查服务器地址和本机 DNA")
    server_urls = resolve_api_base_urls(args)
    static_urls = resolve_static_base_urls(args)
    dna_info = resolve_board_dna(args)
    dna = str(dna_info.get("value") or "").strip().upper()
    if not dna:
        raise DownloadError("live DNA value is empty")
    root = Path(args.out_root)
    public_dir = root / "public"
    private_dir = root / "private"
    legacy_effective_removed = remove_legacy_effective_cache(root)
    token = args.token or os.environ.get("RE_V5_DRIVE_PROFILE_TOKEN", "") or os.environ.get("AX8_DRIVE_PROFILE_TOKEN", "") or read_text_token(args.token_file)

    try:
        raise_if_cancelled(job_token)
        write_server_download_progress("auth_verify", 2, "校验本机授权文件和请求签名")
        device_authorization, device_auth_payload = read_verified_device_authorization(args, dna)
        request_signer: Optional[DeviceRequestSigner] = DeviceRequestSigner(args, dna, device_auth_payload)
    except DeviceGateError as exc:
        write_server_download_progress(
            "auth_verify",
            2,
            exc.message,
            ok=False,
            code="SERVER_DOWNLOAD_DEVICE_GATE",
            failed_stage="auth_verify",
        )
        report = {
            "ok": False,
            "code": "SERVER_DOWNLOAD_DEVICE_GATE",
            "schema": "re-v5-drive-profile-download-v1",
            "generated_at": now_utc(),
            "server_url": server_urls[0] if server_urls else "",
            "server_urls": server_urls,
            "static_urls": static_urls,
            "error_code": exc.error_code,
            "error": exc.message,
            "message_cn": exc.message,
            "license_anchor": {
                "source": dna_info.get("source", "live_hardware"),
                "type": dna_info.get("type", "zynq7000_pl_device_dna_57"),
                "value_stored": False,
                "hash": dna_hash_hex(dna),
            },
            "device_authorization": {
                "stored": Path(args.device_auth_file).is_file(),
                "path": args.device_auth_file,
                "public_key_file": args.device_auth_public_key_file,
                "device_public_key_file": args.device_public_key_file,
            },
            "fresh_download_ok": False,
            "cache_used": False,
            "public_profile_count": 0,
            "private_profile_count": 0,
            "downloaded_profile_count": 0,
            "out_root": str(root),
            "legacy_effective_removed": legacy_effective_removed,
        }
        public_report = local_report(report)
        atomic_write_json(root / "download_status.json", public_report)
        write_result(public_report)
        return public_report

    dna_hash = dna_hash_hex(dna)
    vps_distribution_id = request_signer.device_id if request_signer else ""
    write_server_download_progress("public_download", 3, "下载并校验公共驱动映射表")
    public_result = first_success(
        build_download_urls(server_urls, static_urls, "public", dna_hash, vps_distribution_id),
        token,
        args.timeout,
        public_dir,
        dna,
        device_authorization,
        request_signer,
        job_token,
    )
    probe_private, private_reason = should_probe_private(args, token)
    if probe_private:
        raise_if_cancelled(job_token)
        write_server_download_progress("private_download", 4, "下载并校验私有驱动映射表")
        private_result = first_success(
            build_download_urls(server_urls, static_urls, "private", dna_hash, vps_distribution_id),
            token,
            args.timeout,
            private_dir,
            dna,
            device_authorization,
            request_signer,
            job_token,
        )
    else:
        write_server_download_progress("private_download", 4, "私有驱动映射表未启用，跳过私有下载")
        private_result = {
            "ok": False,
            "skipped": True,
            "reason": private_reason,
            "message": "private profile probe skipped; public/private caches remain separate",
        }
    registration_error = device_gate_error_result(public_result, private_result)

    write_server_download_progress("cache_verify", 5, "校验映射表完整性并写入板端缓存")
    public_map = read_map(public_dir / "driver_profile_map.json")
    private_map = read_map(private_dir / "driver_profile_map.json")
    private_absent_deleted = False
    private_removed_paths: List[str] = []
    active_invalidated: Dict[str, Any] = {"invalidated": False}
    if probe_private and not private_result.get("ok") and download_result_indicates_absent(private_result):
        private_removed_paths = remove_private_cache(private_dir)
        private_map = {}
        private_absent_deleted = True
        private_result["private_absent_deleted"] = True
        private_result["removed_paths"] = private_removed_paths
        active_invalidated = invalidate_private_active_mapping()
    public_result = redact_download_result(public_result, dna)
    private_result = redact_download_result(private_result, dna)
    fresh_download_ok = bool(public_result.get("ok") or private_result.get("ok"))
    public_cache = map_cache_status(public_dir / "driver_profile_map.json", public_map, public_result, "public")
    private_cache = map_cache_status(private_dir / "driver_profile_map.json", private_map, private_result, "private")
    profile_count = int(public_cache["profile_count"]) + int(private_cache["profile_count"])
    public_scope_ok = bool(public_result.get("ok")) or bool(public_cache.get("sha256_verified"))
    private_verified_cache_ok = bool(private_cache.get("sha256_verified"))
    private_scope_ok = (not probe_private) or bool(private_result.get("ok")) or private_absent_deleted or private_verified_cache_ok
    map_integrity_errors: List[str] = []
    if public_result.get("ok") or public_cache["exists"]:
        map_integrity_errors.extend(validate_profile_map(public_map, "public", require_nonempty=True))
    if private_result.get("ok") or private_cache["exists"]:
        map_integrity_errors.extend(validate_profile_map(private_map, "private", require_nonempty=False))
    map_integrity_error = bool(map_integrity_errors)
    ok = bool(
        public_scope_ok
        and private_scope_ok
        and profile_count > 0
        and registration_error is None
        and not map_integrity_error
    )
    cache_used = bool(
        (public_cache.get("sha256_verified") or private_cache.get("sha256_verified")) and
        (not bool(public_result.get("ok")) or (probe_private and not bool(private_result.get("ok"))))
    )
    if ok:
        code = "SERVER_DOWNLOAD_OK"
        if cache_used and not fresh_download_ok:
            message_cn = "服务器下载完成：使用板端已验证公共/私有映射表缓存"
        elif cache_used and probe_private and not bool(private_result.get("ok")):
            message_cn = "服务器下载完成：公共映射表已同步，私有映射表使用板端已验证缓存"
        elif private_cache["exists"]:
            message_cn = "服务器下载完成：公共和私有映射表已同步到板端"
        elif private_absent_deleted:
            message_cn = "服务器下载完成：公共映射表已同步，私有映射表远端不存在并已删除本地缓存"
        else:
            message_cn = "服务器下载完成：公共映射表已同步，私有映射表未启用"
    elif cache_used:
        code = "SERVER_DOWNLOAD_CACHE_ONLY"
        message_cn = "服务器下载失败，保留已有公共/私有缓存：映射表%d个" % profile_count
    else:
        code = "SERVER_DOWNLOAD_FAIL"
        message_cn = "服务器下载失败，未更新映射表"
    if map_integrity_error and registration_error is None:
        code = "SERVER_DOWNLOAD_FAIL"
        message_cn = "服务器下载失败：映射表完整性校验未通过"
    if registration_error is not None:
        code = "SERVER_DOWNLOAD_DEVICE_GATE"
        message_cn = str(registration_error.get("message") or DEVICE_GATE_MESSAGES.get(str(registration_error.get("error_code")), message_cn))
    failed_stage = ""
    if not ok:
        if registration_error is not None:
            failed_stage = "device_gate"
        elif map_integrity_error:
            failed_stage = "map_integrity"
        elif cache_used:
            failed_stage = "fresh_download"
        else:
            failed_stage = "download_result"
    write_server_download_progress(
        "result",
        6,
        message_cn,
        ok=ok,
        code=code,
        failed_stage=failed_stage,
        extra={
            "public_profile_count": public_cache["profile_count"],
            "private_profile_count": private_cache["profile_count"],
            "fresh_download_ok": fresh_download_ok,
            "cache_used": cache_used,
        },
    )

    report = {
        "ok": ok,
        "code": code,
        "schema": "re-v5-drive-profile-download-v1",
        "generated_at": now_utc(),
        "server_url": server_urls[0] if server_urls else "",
        "server_urls": server_urls,
        "static_urls": static_urls,
        "request_timeout_sec": args.timeout,
        "error_code": registration_error.get("error_code", "") if registration_error else "",
        "error": registration_error.get("message", "") if registration_error else ("map_integrity_error" if map_integrity_error else ""),
        "message_cn": message_cn,
        "license_anchor": {
            "source": dna_info.get("source", "live_hardware"),
            "type": dna_info.get("type", "zynq7000_pl_device_dna_57"),
            "value_stored": False,
            "hash": dna_hash_hex(dna),
        },
        "device_authorization": {
            "stored": True,
            "path": args.device_auth_file,
            "key_id": device_auth_payload.get("key_id", ""),
            "device_id": vps_distribution_id,
            "vps_distribution_id": vps_distribution_id,
            "pl_dna_hash": device_auth_payload.get("pl_device_dna_hash", ""),
            "device_public_key_sha256": device_auth_payload.get("device_public_key_sha256", ""),
        },
        "public": public_result,
        "private": private_result,
        "private_probe": {
            "enabled": probe_private,
            "reason": private_reason,
        },
        "profile_map_paths": {
            "public": str(public_dir / "driver_profile_map.json"),
            "private": str(private_dir / "driver_profile_map.json"),
        },
        "profile_map_exists": {
            "public": (public_dir / "driver_profile_map.json").is_file(),
            "private": (private_dir / "driver_profile_map.json").is_file(),
        },
        "map_cache": {
            "public": public_cache,
            "private": private_cache,
        },
        "private_absent_deleted": private_absent_deleted,
        "deleted_private_local": private_absent_deleted,
        "private_removed_paths": private_removed_paths,
        "active_invalidated": active_invalidated,
        "legacy_effective_removed": legacy_effective_removed,
        "profile_fetch_error": False,
        "map_integrity_error": map_integrity_error,
        "map_integrity_errors": map_integrity_errors,
        "fresh_download_ok": fresh_download_ok,
        "cache_used": cache_used,
        "public_profile_count": public_cache["profile_count"],
        "private_profile_count": private_cache["profile_count"],
        "downloaded_profile_count": profile_count,
        "out_root": str(root),
    }
    public_report = local_report(report)
    atomic_write_json(root / "download_status.json", public_report)
    write_result(public_report)
    return public_report

def run_action(request: Dict[str, Any], context: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    global REQUEST_STATUS_EPOCH

    job_token = job_token_from_context(context)
    raise_if_cancelled(job_token)
    install_ipv4_urlopen_resolution()
    REQUEST_STATUS_EPOCH = request_status_epoch(request)
    write_server_download_progress("request_check", 1, "检查服务器下载请求和状态帧")
    stale_result = stale_epoch_result(REQUEST_STATUS_EPOCH, current_status_epoch())
    if stale_result is not None:
        report = attach_request_status_epoch(stale_result)
        write_server_download_progress(
            "request_check",
            1,
            str(report.get("message_cn") or "服务器下载请求状态不可用"),
            ok=False,
            code=str(report.get("code") or "SERVER_DOWNLOAD_STALE_EPOCH"),
            failed_stage="request_check",
        )
        write_result(report)
        return report

    args = parse_args([])
    try:
        report = run(args, job_token=job_token)
    except DeviceGateError as exc:
        write_server_download_progress(
            "auth_verify",
            2,
            exc.message,
            ok=False,
            code="SERVER_DOWNLOAD_DEVICE_GATE",
            failed_stage="auth_verify",
        )
        report = {
            "ok": False,
            "code": "SERVER_DOWNLOAD_DEVICE_GATE",
            "schema": "re-v5-drive-profile-download-v1",
            "generated_at": now_utc(),
            "error_code": exc.error_code,
            "error": exc.message,
            "message_cn": exc.message,
        }
        report = attach_request_status_epoch(report)
        write_result(report)
        return report
    except Exception as exc:
        write_server_download_progress(
            "result",
            6,
            "服务器下载失败，未隐藏 native 错误",
            ok=False,
            code="SERVER_DOWNLOAD_FAIL",
            failed_stage="unhandled_exception",
        )
        report = {
            "ok": False,
            "code": "SERVER_DOWNLOAD_FAIL",
            "schema": "re-v5-drive-profile-download-v1",
            "generated_at": now_utc(),
            "error": repr(exc),
            "message_cn": "服务器下载失败，未隐藏 native 错误",
        }
        report = attach_request_status_epoch(report)
        write_result(report)
        return report
    report = attach_request_status_epoch(report)
    write_result(report)
    return report

def main() -> int:
    args = parse_args()
    install_ipv4_urlopen_resolution()
    request_path = Path(args.request) if args.request else None
    if request_path is not None:
        report = run_action(read_request(request_path))
    else:
        try:
            report = run(args)
        except DeviceGateError as exc:
            write_server_download_progress(
                "auth_verify",
                2,
                exc.message,
                ok=False,
                code="SERVER_DOWNLOAD_DEVICE_GATE",
                failed_stage="auth_verify",
            )
            report = {
                "ok": False,
                "code": "SERVER_DOWNLOAD_DEVICE_GATE",
                "schema": "re-v5-drive-profile-download-v1",
                "generated_at": now_utc(),
                "error_code": exc.error_code,
                "error": exc.message,
                "message_cn": exc.message,
            }
            write_result(report)
        except Exception as exc:
            write_server_download_progress(
                "result",
                6,
                "服务器下载失败，未隐藏 native 错误",
                ok=False,
                code="SERVER_DOWNLOAD_FAIL",
                failed_stage="unhandled_exception",
            )
            report = {
                "ok": False,
                "code": "SERVER_DOWNLOAD_FAIL",
                "schema": "re-v5-drive-profile-download-v1",
                "generated_at": now_utc(),
                "error": repr(exc),
                "message_cn": "服务器下载失败，未隐藏 native 错误",
            }
            write_result(report)
    print(json.dumps(report, ensure_ascii=False, sort_keys=True))
    return 0 if report.get("ok") else 2

