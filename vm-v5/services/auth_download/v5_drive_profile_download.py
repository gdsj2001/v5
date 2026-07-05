#!/usr/bin/env python3
"""Download public/private drive profile maps for the RE v5 board.

This follows the 8AX server-download contract but keeps the v5 script
self-contained around the existing v5 Device DNA registration modules.
The script reads live PL DNA, verifies the local VPS-signed device
authorization, signs each protected request with the device private key, and
updates /opt/8ax/drive-profiles only after a verified download path succeeds.
"""

from __future__ import annotations
from drive_profile_download_errors import DownloadError
from drive_profile_download_endpoints import append_urls, load_endpoint_config, normalize_base_url, resolve_api_base_urls, resolve_static_base_urls, split_urls, unique_urls
from drive_profile_download_core import atomic_write, atomic_write_json, b64url_encode, canonical_json_bytes, load_json_bytes, now_utc, parse_sha256_sidecar, read_text_token, sha256_bytes
from drive_profile_download_status import STALE_EPOCH_MAX_AGE_NS, current_status_epoch, read_request, request_status_epoch, stale_epoch_result
from drive_profile_download_ipv4 import _IPV4_RESOLUTION_INSTALLED, _ORIGINAL_GETADDRINFO, force_ipv4_enabled, install_ipv4_urlopen_resolution
from drive_profile_download_args import DEFAULT_DEVICE_AUTH_FILE, DEFAULT_DEVICE_AUTH_PUBLIC_KEY_FILE, DEFAULT_DEVICE_PRIVATE_KEY_FILE, DEFAULT_DEVICE_PUBLIC_KEY_FILE, DEFAULT_OUT_ROOT, DEFAULT_TOKEN_FILE, DEFAULT_VPS_ENDPOINTS_CONFIG, parse_args
from drive_profile_download_flow import RUN_DIR, REQUEST_STATUS_EPOCH, SERVER_DOWNLOAD_PROGRESS_SCHEMA, SERVER_DOWNLOAD_PROGRESS_TOTAL, attach_request_status_epoch, job_token_from_context, main, run, run_action, server_download_progress_path, write_result, write_server_download_progress
from drive_profile_download_auth import (
    DEVICE_AUTHORIZATION_INVALID_CODE,
    DEVICE_AUTHORIZATION_MISSING_CODE,
    DEVICE_GATE_CODES,
    DEVICE_GATE_MESSAGES,
    DEVICE_NOT_REGISTERED_CODE,
    DEVICE_PUBLIC_KEY_MISSING_CODE,
    DEVICE_REQUEST_SIGNATURE_ALG,
    DEVICE_REQUEST_SIGNATURE_INVALID_CODE,
    DEVICE_REQUEST_SIGNATURE_SCHEMA,
    DeviceGateError,
    DeviceRequestSigner,
    auth_error,
    base_url_for_url,
    compact_device_authorization,
    describe_http_error,
    dna_hash_hex,
    http_post_json,
    openssl_sign_private,
    parse_iso_utc,
    read_verified_device_authorization,
    request_device_challenge,
    request_path_for_url,
    resolve_board_dna,
)
from drive_profile_download_cache import (
    invalidate_private_active_mapping,
    remove_legacy_effective_cache,
    remove_private_cache,
)
from drive_profile_download_transport import (
    build_download_urls,
    device_gate_error_result,
    download_result_indicates_absent,
    first_success,
    http_get,
    raise_if_cancelled,
    redact_download_result,
    redact_sensitive_download_text,
    should_probe_private,
)
from drive_profile_download_install import (
    RELEASE_CONTRACT_KEY,
    _nonempty_string,
    install_json_payload,
    install_payload,
    install_tar,
    install_zip,
    map_cache_status,
    profile_count,
    read_map,
    release_contract_status,
    safe_rel_path,
    sha256_file,
    validate_profile_commands,
    validate_profile_map,
    validate_release_contract,
)















































































































































if __name__ == "__main__":
    raise SystemExit(main())
