from __future__ import annotations

import re
import subprocess
from pathlib import Path
from typing import Any, Dict, List


BACKEND_READINESS_PROBE = Path("/usr/libexec/8ax/v5_backend_readiness_probe")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
MAX_PROBE_OUTPUT_BYTES = 16384


class DriveAttestationError(RuntimeError):
    def __init__(self, code: str, detail: Any = None) -> None:
        super().__init__(code)
        self.code = str(code)
        self.detail = detail


def _probe(argv: List[str], timeout_s: float) -> Dict[str, str]:
    try:
        completed = subprocess.run(
            [str(BACKEND_READINESS_PROBE), *argv],
            check=False,
            capture_output=True,
            text=True,
            timeout=max(0.1, float(timeout_s)),
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise DriveAttestationError(
            "DRIVE_NATIVE_ATTESTATION_PROBE_UNAVAILABLE",
            "%s: %s" % (type(exc).__name__, exc)) from exc
    output = (completed.stdout or "") + (completed.stderr or "")
    if len(output.encode("utf-8", errors="replace")) > MAX_PROBE_OUTPUT_BYTES:
        raise DriveAttestationError(
            "DRIVE_NATIVE_ATTESTATION_OUTPUT_OVERSIZE", len(output))
    fields: Dict[str, str] = {}
    for token in output.strip().split():
        if "=" in token:
            key, value = token.split("=", 1)
            fields[key] = value
    if completed.returncode != 0:
        raise DriveAttestationError(
            fields.get("code") or "DRIVE_NATIVE_ATTESTATION_PROBE_FAILED",
            {"returncode": completed.returncode, "fields": fields})
    for key in (
            "code", "generation", "owner_pid", "owner_start_ticks",
            "data_ready", "motion_ready", "drive_verified", "slaves"):
        if key not in fields:
            raise DriveAttestationError(
                "DRIVE_NATIVE_ATTESTATION_RESPONSE_INCOMPLETE",
                {"missing": key, "fields": fields})
    return fields


def _positive_int(fields: Dict[str, str], key: str) -> int:
    try:
        value = int(fields.get(key, ""), 10)
    except ValueError as exc:
        raise DriveAttestationError(
            "DRIVE_NATIVE_ATTESTATION_RESPONSE_INVALID",
            {"field": key, "value": fields.get(key)}) from exc
    if value <= 0:
        raise DriveAttestationError(
            "DRIVE_NATIVE_ATTESTATION_RESPONSE_INVALID",
            {"field": key, "value": value})
    return value


def backend_data_identity(timeout_s: float) -> Dict[str, Any]:
    fields = _probe(["--require", "data"], timeout_s)
    if fields.get("data_ready") != "1":
        raise DriveAttestationError(
            "DRIVE_NATIVE_BACKEND_DATA_NOT_READY", fields)
    slave_parts = fields["slaves"].split("/", 1)
    if len(slave_parts) != 2:
        raise DriveAttestationError(
            "DRIVE_NATIVE_ATTESTATION_RESPONSE_INVALID", fields)
    try:
        actual_slaves = int(slave_parts[0], 10)
        expected_slaves = int(slave_parts[1], 10)
    except ValueError as exc:
        raise DriveAttestationError(
            "DRIVE_NATIVE_ATTESTATION_RESPONSE_INVALID", fields) from exc
    if actual_slaves <= 0 or actual_slaves != expected_slaves:
        raise DriveAttestationError(
            "DRIVE_NATIVE_SLAVE_COUNT_MISMATCH", fields)
    return {
        "generation": _positive_int(fields, "generation"),
        "owner_pid": _positive_int(fields, "owner_pid"),
        "owner_start_ticks": _positive_int(fields, "owner_start_ticks"),
        "expected_slaves": expected_slaves,
        "fields": fields,
    }


def attest_boot_drive_generation(
        drive_apply: Dict[str, Any], timeout_s: float = 3.0) -> Dict[str, Any]:
    if not isinstance(drive_apply, dict) or not drive_apply.get("ok"):
        raise DriveAttestationError(
            "DRIVE_NATIVE_ATTESTATION_APPLY_NOT_OK", drive_apply)
    identity = drive_apply.get("drive_transaction_identity")
    if not isinstance(identity, dict):
        raise DriveAttestationError(
            "DRIVE_NATIVE_ATTESTATION_IDENTITY_MISSING", identity)
    transaction_sha256 = str(
        identity.get("transaction_generation") or "").lower()
    if not SHA256_RE.fullmatch(transaction_sha256):
        raise DriveAttestationError(
            "DRIVE_NATIVE_ATTESTATION_IDENTITY_INVALID", transaction_sha256)
    try:
        native_mapping_generation = int(
            identity.get("native_mapping_generation") or 0)
        persistent_mapping_generation = int(
            identity.get("persistent_mapping_generation") or 0)
    except (TypeError, ValueError) as exc:
        raise DriveAttestationError(
            "DRIVE_NATIVE_ATTESTATION_MAPPING_INVALID", identity) from exc
    if (native_mapping_generation <= 0 or
            native_mapping_generation != persistent_mapping_generation or
            not identity.get("native_mapping_matches_persistent")):
        raise DriveAttestationError(
            "DRIVE_NATIVE_ATTESTATION_MAPPING_MISMATCH", identity)
    try:
        started_ns = int(
            drive_apply.get("batch_readback_started_monotonic_ns") or 0)
    except (TypeError, ValueError) as exc:
        raise DriveAttestationError(
            "DRIVE_NATIVE_ATTESTATION_READBACK_TIME_INVALID",
            drive_apply.get("batch_readback_started_monotonic_ns")) from exc
    if started_ns <= 0:
        raise DriveAttestationError(
            "DRIVE_NATIVE_ATTESTATION_READBACK_TIME_MISSING", started_ns)
    backend = backend_data_identity(timeout_s)
    targets = drive_apply.get("targets")
    if not isinstance(targets, list) or len(targets) != backend["expected_slaves"]:
        raise DriveAttestationError(
            "DRIVE_NATIVE_ATTESTATION_TARGET_COUNT_MISMATCH", targets)
    slots = []
    for target in targets:
        if (not isinstance(target, dict) or not target.get("ok") or
                target.get("code") != "DRIVE_SET_TARGET_OK" or
                not isinstance(target.get("readback"), dict)):
            raise DriveAttestationError(
                "DRIVE_NATIVE_ATTESTATION_TARGET_READBACK_INVALID", target)
        try:
            slot = int(target.get("status_slot"))
        except (TypeError, ValueError) as exc:
            raise DriveAttestationError(
                "DRIVE_NATIVE_ATTESTATION_STATUS_SLOT_INVALID", target) from exc
        if slot < 0 or slot >= 32:
            raise DriveAttestationError(
                "DRIVE_NATIVE_ATTESTATION_STATUS_SLOT_INVALID", target)
        slots.append(slot)
    if sorted(slots) != list(range(backend["expected_slaves"])):
        raise DriveAttestationError(
            "DRIVE_NATIVE_ATTESTATION_STATUS_SLOT_MISMATCH", slots)
    axis_mask = sum(1 << slot for slot in slots)
    fields = _probe([
        "--verify-drive",
        "--drive-owner-generation", str(backend["generation"]),
        "--drive-mapping-generation", str(native_mapping_generation),
        "--drive-axis-mask", str(axis_mask),
        "--drive-readback-count", str(len(targets)),
        "--drive-readback-start-ns", str(started_ns),
        "--drive-transaction-sha256", transaction_sha256,
    ], timeout_s)
    expected_readback = {
        "generation": backend["generation"],
        "owner_pid": backend["owner_pid"],
        "owner_start_ticks": backend["owner_start_ticks"],
        "drive_mapping_generation": native_mapping_generation,
        "drive_axis_mask": axis_mask,
        "drive_readback_count": len(targets),
        "drive_readback_started": started_ns,
    }
    try:
        actual_readback = {
            key: int(fields.get(key, ""), 10)
            for key in expected_readback
        }
        drive_verified_at = int(fields.get("drive_verified_at", ""), 10)
    except ValueError as exc:
        raise DriveAttestationError(
            "DRIVE_NATIVE_ATTESTATION_READBACK_INVALID", fields) from exc
    if (fields.get("drive_verified") != "1" or
            fields.get("motion_ready") != "1" or
            fields.get("drive_identity_sha256") != transaction_sha256 or
            actual_readback != expected_readback or
            drive_verified_at <= 0):
        raise DriveAttestationError(
            "DRIVE_NATIVE_ATTESTATION_READBACK_MISMATCH", fields)
    return {
        "ok": True,
        "code": fields["code"],
        "generation": backend["generation"],
        "owner_pid": backend["owner_pid"],
        "owner_start_ticks": backend["owner_start_ticks"],
        "mapping_generation": native_mapping_generation,
        "axis_mask": axis_mask,
        "readback_count": len(targets),
        "readback_started_monotonic_ns": started_ns,
        "transaction_sha256": transaction_sha256,
        "motion_ready": True,
    }


def invalidate_drive_generation(timeout_s: float = 3.0) -> Dict[str, Any]:
    backend = backend_data_identity(timeout_s)
    fields = _probe([
        "--invalidate-drive",
        "--drive-owner-generation", str(backend["generation"]),
    ], timeout_s)
    try:
        response_identity = {
            "generation": int(fields.get("generation", ""), 10),
            "owner_pid": int(fields.get("owner_pid", ""), 10),
            "owner_start_ticks": int(fields.get("owner_start_ticks", ""), 10),
        }
    except ValueError as exc:
        raise DriveAttestationError(
            "DRIVE_NATIVE_INVALIDATION_READBACK_INVALID", fields) from exc
    if (fields.get("drive_verified") != "0" or
            fields.get("motion_ready") != "0" or
            response_identity != {
                "generation": backend["generation"],
                "owner_pid": backend["owner_pid"],
                "owner_start_ticks": backend["owner_start_ticks"],
            }):
        raise DriveAttestationError(
            "DRIVE_NATIVE_INVALIDATION_READBACK_MISMATCH", fields)
    return {
        "ok": True,
        "code": fields["code"],
        "generation": backend["generation"],
        "owner_pid": backend["owner_pid"],
        "owner_start_ticks": backend["owner_start_ticks"],
        "motion_ready": False,
        "drive_verified": False,
    }
