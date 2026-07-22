#!/usr/bin/env python3
"""Typed Command Gate client for the BUS axis-zero live MCS commit."""

from __future__ import annotations

import ctypes
import os
import socket
import struct
from typing import Any, Dict


MAGIC = 0x56354347
VERSION = 6
OP_SETTINGS_AXIS_ZERO_LIVE_APPLY = 7
OP_PROBE_AXIS_SLAVE_MAPPING = 6
DEFAULT_SOCKET = "/run/8ax_v5_product_ui/v5_command_gate.sock"


class Request(ctypes.Structure):
    _fields_ = [
        ("magic", ctypes.c_uint32), ("version", ctypes.c_uint32),
        ("size", ctypes.c_uint32), ("op", ctypes.c_uint32),
        ("kind", ctypes.c_int32), ("index_value", ctypes.c_int32),
        ("enabled_value", ctypes.c_int32), ("axis_mask", ctypes.c_uint32),
        ("home_run_id", ctypes.c_uint64), ("home_generation", ctypes.c_uint32),
        ("estop_clean_generation", ctypes.c_uint32),
        ("axis_value", ctypes.c_double), ("increment_value", ctypes.c_double),
        ("point_axis", ctypes.c_double * 5), ("text_value", ctypes.c_char * 512),
        ("secondary_text_value", ctypes.c_char * 128),
        ("mode_value", ctypes.c_char * 64),
        ("settings_axis_index", ctypes.c_uint32),
        ("settings_owner_generation", ctypes.c_uint32),
        ("settings_readback_token", ctypes.c_uint32),
        ("settings_project_root", ctypes.c_char * 256),
        ("settings_axis", ctypes.c_char * 16),
        ("settings_field_key", ctypes.c_char * 64),
        ("settings_field_name", ctypes.c_char * 128),
        ("settings_value_text", ctypes.c_char * 128),
    ]


class Response(ctypes.Structure):
    _fields_ = [
        ("magic", ctypes.c_uint32), ("version", ctypes.c_uint32),
        ("size", ctypes.c_uint32), ("send_status", ctypes.c_int32),
        ("executed", ctypes.c_int32), ("machine_on_requested", ctypes.c_int32),
        ("machine_on_status", ctypes.c_int32), ("safety_estop_known", ctypes.c_int32),
        ("safety_estop_active", ctypes.c_int32), ("machine_enable_known", ctypes.c_int32),
        ("machine_enabled", ctypes.c_int32),
        ("axis_slave_mapping_status_available", ctypes.c_int32),
        ("axis_slave_mapping_applicable", ctypes.c_int32),
        ("axis_slave_mapping_valid", ctypes.c_int32),
        ("axis_slave_mapping_generation", ctypes.c_uint32),
        ("axis_slave_mapping_code", ctypes.c_char * 64),
        ("estop_clean_generation", ctypes.c_uint32),
        ("estop_clean_active", ctypes.c_int32), ("estop_clean_terminal", ctypes.c_int32),
        ("estop_clean_ok", ctypes.c_int32), ("estop_clean_code", ctypes.c_char * 64),
        ("command_line", ctypes.c_char * 384), ("readback_code", ctypes.c_char * 64),
        ("settings_owner_written", ctypes.c_int32),
        ("settings_source_readback_confirmed", ctypes.c_int32),
        ("settings_restart_pending", ctypes.c_int32),
        ("settings_scale_chain_attempted", ctypes.c_int32),
        ("settings_scale_recomputed", ctypes.c_int32),
        ("settings_raw_limits_recomputed", ctypes.c_int32),
        ("settings_effective_scale", ctypes.c_double),
        ("settings_raw_zero_position", ctypes.c_double),
        ("settings_raw_min_limit", ctypes.c_double),
        ("settings_raw_max_limit", ctypes.c_double),
        ("settings_readback_value", ctypes.c_char * 64),
        ("settings_scale_chain_code", ctypes.c_char * 64),
        ("home_run_id", ctypes.c_uint64), ("home_generation", ctypes.c_uint32),
        ("home_phase", ctypes.c_uint32), ("home_failure_phase", ctypes.c_uint32),
        ("home_current_axis_mask", ctypes.c_uint32), ("home_active", ctypes.c_int32),
        ("home_terminal", ctypes.c_int32), ("home_cancelled", ctypes.c_int32),
        ("home_detail_valid", ctypes.c_int32), ("home_actual", ctypes.c_double),
        ("home_target", ctypes.c_double), ("home_mode", ctypes.c_char * 8),
        ("home_current_axes", ctypes.c_char * 16),
        ("home_direct_reason", ctypes.c_char * 64),
        ("zero_commit_seq", ctypes.c_uint32),
        ("zero_display_verified", ctypes.c_int32),
        ("zero_mcs_position", ctypes.c_double),
        ("zero_tolerance_units", ctypes.c_double),
        ("zero_previous_mcs_position", ctypes.c_double),
    ]


def _text(value: bytes) -> str:
    return value.split(b"\0", 1)[0].decode("utf-8", errors="replace")


def _transact(request: Request, timeout_s: float) -> Response:
    path = os.environ.get("V5_COMMAND_GATE_SOCKET", DEFAULT_SOCKET)
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(max(0.1, float(timeout_s)))
        client.connect(path)
        client.sendall(bytes(request))
        payload = b""
        while len(payload) < 12:
            chunk = client.recv(4096)
            if not chunk:
                raise RuntimeError("command gate EOF before response header")
            payload += chunk
        declared = struct.unpack_from("<I", payload, 8)[0]
        if declared != ctypes.sizeof(Response):
            raise RuntimeError("command gate response ABI mismatch: %d != %d" %
                               (declared, ctypes.sizeof(Response)))
        while len(payload) < declared:
            chunk = client.recv(declared - len(payload))
            if not chunk:
                raise RuntimeError("command gate EOF before response body")
            payload += chunk
    response = Response.from_buffer_copy(payload)
    if response.magic != MAGIC or response.version != VERSION or response.size != len(payload):
        raise RuntimeError("command gate response envelope invalid")
    return response


def probe_axis_slave_mapping(timeout_s: float = 1.0) -> Dict[str, Any]:
    request = Request()
    request.magic = MAGIC
    request.version = VERSION
    request.size = ctypes.sizeof(Request)
    request.op = OP_PROBE_AXIS_SLAVE_MAPPING
    response = _transact(request, timeout_s)
    available = bool(response.axis_slave_mapping_status_available)
    applicable = bool(response.axis_slave_mapping_applicable)
    valid = bool(response.axis_slave_mapping_valid)
    generation = int(response.axis_slave_mapping_generation)
    return {
        "ok": response.send_status == 1 and available and applicable and valid and generation != 0,
        "send_status": int(response.send_status),
        "available": available,
        "applicable": applicable,
        "valid": valid,
        "generation": generation,
        "code": _text(bytes(response.axis_slave_mapping_code)),
        "readback_code": _text(bytes(response.readback_code)),
    }


def apply_axis_zero_live(axis: str, slave_position: int, run_id: str,
                         timeout_s: float = 1.0,
                         expected_mcs_position: float = 0.0) -> Dict[str, Any]:
    axis = str(axis or "").upper()
    if len(axis) != 1 or axis not in "XYZABC":
        raise ValueError("axis must be one of XYZABC")
    if not 0 <= int(slave_position) < 5:
        raise ValueError("slave_position must be in 0..4")
    if not run_id:
        raise ValueError("run_id is required")
    request = Request()
    request.magic = MAGIC
    request.version = VERSION
    request.size = ctypes.sizeof(Request)
    request.op = OP_SETTINGS_AXIS_ZERO_LIVE_APPLY
    request.index_value = int(slave_position)
    request.axis_value = float(expected_mcs_position)
    request.text_value = str(run_id).encode("utf-8")[:511]
    request.settings_axis = axis.encode("ascii")
    response = _transact(request, timeout_s)
    return {
        "ok": response.send_status == 1 and bool(response.executed) and
              bool(response.zero_display_verified) and response.zero_commit_seq != 0,
        "send_status": int(response.send_status),
        "executed": bool(response.executed),
        "code": _text(bytes(response.readback_code)),
        "machine_enable_known": bool(response.machine_enable_known),
        "machine_enabled": bool(response.machine_enabled),
        "restart_pending": bool(response.settings_restart_pending),
        "commit_seq": int(response.zero_commit_seq),
        "zero_display_verified": bool(response.zero_display_verified),
        "mcs_position": float(response.zero_mcs_position),
        "tolerance_units": float(response.zero_tolerance_units),
        "previous_mcs_position": float(response.zero_previous_mcs_position),
    }
