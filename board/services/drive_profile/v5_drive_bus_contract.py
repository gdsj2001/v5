from __future__ import annotations

import math
import os
import time
from pathlib import Path
from typing import Any, Dict, List, Tuple

RUN_DIR = Path("/run/8ax_v5_drive")
RESIDENT_SNAPSHOT = RUN_DIR / "drive_profile_resident_snapshot.json"
PROJECT_ROOT = Path(os.environ.get("V5_PROJECT_ROOT", "/opt/8ax/v5"))
SELF_PARAMETER_TABLE = PROJECT_ROOT / "config/settings/self_parameter_table.tsv"
DRIVE_PARAMETER_TABLE = PROJECT_ROOT / "config/settings/drive_parameter_table.tsv"
SETTINGS_RUNTIME_JSON = Path(os.environ.get("V5_SETTINGS_RUNTIME_JSON", "/opt/8ax/phase0_bus5/settings_runtime.json"))
RUNTIME_SETTINGS_INI = Path(os.environ.get("V5_RUNTIME_SETTINGS_INI", str(PROJECT_ROOT / "linuxcnc/ini/v5_bus.ini")))

MAX_COMMAND_OUTPUT_BYTES = 16000
MAX_RESULT_JSON_BYTES = 65536
AXIS_ORDER = ("X", "Y", "Z", "A", "B", "C", "GANTRY", "TOOLMAG")
REQUIRED_READ_COMMANDS = (
    "drive.read_statusword",
    "drive.read_error_code",
    "drive.read_mode",
    "drive.read_actual_position",
    "drive.read_egear",
)
OPTIONAL_READ_COMMANDS = (
    "drive.read_actual_velocity",
    "drive.read_abs_multi_turn",
    "drive.read_abs_single_turn",
    "drive.read_abs_position",
    "drive.read_torque",
    "drive.read_current",
    "drive.read_temperature",
    "drive.read_dc_link_voltage",
    "drive.read_bus_state",
    "drive.read_following_error",
    "drive.read_io_status",
)
CANONICAL_CSP_MODE = 8
STATUSWORD_FAULT_BIT = 0x0008
STATUSWORD_OPERATION_ENABLED_BIT = 0x0004
TYPE_MAP = {
    "bool": "bool",
    "int8": "int8",
    "uint8": "uint8",
    "int16": "int16",
    "uint16": "uint16",
    "int32": "int32",
    "uint32": "uint32",
    "int64": "int64",
    "uint64": "uint64",
    "float": "float",
    "real32": "float",
    "double": "double",
    "real64": "double",
}
SETTINGS_RUNTIME_SCHEMA = "re.v3.settings_runtime.drive_only.v1"
MAX_SETTINGS_RUNTIME_NODES = 20000
MAX_SETTINGS_RUNTIME_DEPTH = 24
SETTINGS_RUNTIME_FORBIDDEN_KEYS = frozenset({
    "active_wcs",
    "config_cache",
    "current_tool",
    "g5x",
    "g92",
    "g92_offset",
    "hal_snapshot",
    "ini_readback",
    "linuxcnc_snapshot_after",
    "linuxcnc_snapshot_before",
    "machine_state",
    "modal",
    "motion_state",
    "native_status",
    "post_status",
    "pre_status",
    "rtcp",
    "rtcp_actual",
    "runtime_ini",
    "runtime_ini_readback",
    "runtime_ini_sections",
    "runtime_modal_text",
    "safety",
    "settings_projection",
    "shm",
    "shm_frame",
    "status",
    "status_epoch",
    "status_frame",
    "stillness",
    "tlo",
    "tool",
    "tool_length",
    "tool_table",
    "wcs",
    "wcs_offset",
    "wcs_offsets",
})
SETTINGS_RUNTIME_FORBIDDEN_PREFIXES = (
    "g92_",
    "modal_",
    "rtcp_",
    "tlo_",
    "tool_",
    "wcs_",
)
SETTINGS_RUNTIME_WRITE_DROP_KEYS = frozenset({
    "axis_section",
    "ini_readback",
    "joint_section",
    "linuxcnc_snapshot_after",
    "linuxcnc_snapshot_before",
    "post_status",
    "pre_status",
    "raw_limit_save",
    "g53_geometry",
    "rtcp_g53_geometry",
    "rtcp_geometry",
    "runtime_ini",
    "runtime_ini_readback",
    "runtime_ini_sections",
    "runtime_scale_check_after",
    "runtime_scale_check_before",
    "status_frame",
    "updated_sections",
})

class DriveActionError(RuntimeError):
    def __init__(self, code: str, message_cn: str, detail: Any = None) -> None:
        super().__init__(message_cn)
        self.code = code
        self.message_cn = message_cn
        self.detail = detail


def now_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def finite_float(value: Any) -> float | None:
    try:
        number = float(value)
    except Exception:
        return None
    return number if math.isfinite(number) else None


def axis_unit(axis: str) -> str:
    return "deg" if str(axis or "").upper() in {"A", "B", "C"} else "mm"
