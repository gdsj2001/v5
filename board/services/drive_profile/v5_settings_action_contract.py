from __future__ import annotations

from pathlib import Path
from typing import Dict

RUN_DIR = Path("/run/8ax_v5_product_ui")
SOCKET_PATH = RUN_DIR / "settings_actiond.sock"
EVENT_LOG = RUN_DIR / "settings_actiond_events.jsonl"
DRIVE_TIMEOUT_S = 8.0
DRIVE_ACTION_TIMEOUTS: Dict[str, float] = {
    "scan": 20.0,
    "read": 45.0,
    "fault-reset": 45.0,
    "factory-reset": 90.0,
    "feedforward": 180.0,
    "set-drive": 180.0,
    "axis-zero": 20.0,
}
MAX_ACTIOND_REQUEST_BYTES = 4096
MAX_ACTIOND_RESULT_BYTES = 65536
MAX_ACTIOND_EVENT_BYTES = 8192
SERVICE_RESTART_TIMEOUTS: Dict[str, float] = {
    "v5-linuxcnc-command-gate": 35.0,
    "v5-wcs-status-publisher": 20.0,
    "v5-state-publisher": 12.0,
    "v5-ui-relay": 15.0,
}
CANONICAL_CLEAN_RESTART_SERVICES = [
    "v5-ui-relay",
    "v5-touch-diagnostics",
    "v5-settings-actiond",
    "v5-state-publisher",
    "v5-wcs-status-publisher",
    "v5-linuxcnc-command-gate",
]
