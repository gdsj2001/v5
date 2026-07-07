from __future__ import annotations

import argparse
import os
from typing import List, Optional

DEFAULT_VPS_ENDPOINTS_CONFIG = "/etc/6x-cnc/vps_endpoints.json"
DEFAULT_OUT_ROOT = "/opt/8ax/drive-profiles"
DEFAULT_TOKEN_FILE = "/etc/6x-cnc/bus_client_token"
DEFAULT_DEVICE_AUTH_FILE = "/etc/6x-cnc/device_authorization.json"
DEFAULT_DEVICE_AUTH_PUBLIC_KEY_FILE = "/etc/6x-cnc/device_auth_public.pem"
DEFAULT_DEVICE_PRIVATE_KEY_FILE = "/etc/6x-cnc/device_private_key.pem"
DEFAULT_DEVICE_PUBLIC_KEY_FILE = "/etc/6x-cnc/device_public_key.pem"
DEFAULT_REGISTER_STATUS_PATH = "/opt/8ax/drive-profiles/device_register_status.json"


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Download public/private drive profile maps for this RE v5 board.")
    parser.add_argument("--vps-endpoints-config", default=os.environ.get("RE_V5_VPS_ENDPOINTS_CONFIG", DEFAULT_VPS_ENDPOINTS_CONFIG))
    parser.add_argument("--server-url", default="")
    parser.add_argument("--static-url", default="")
    parser.add_argument("--out-root", default=os.environ.get("RE_V5_DRIVE_PROFILE_ROOT", DEFAULT_OUT_ROOT))
    parser.add_argument("--device-dna", default="")
    parser.add_argument("--token", default="")
    parser.add_argument("--token-file", default=os.environ.get("RE_V5_DRIVE_PROFILE_TOKEN_FILE", DEFAULT_TOKEN_FILE))
    parser.add_argument("--device-auth-file", default=os.environ.get("RE_V5_DEVICE_AUTH_FILE", DEFAULT_DEVICE_AUTH_FILE))
    parser.add_argument("--register-status-path", default=os.environ.get("RE_V5_DEVICE_DNA_REGISTER_STATUS_PATH", DEFAULT_REGISTER_STATUS_PATH))
    parser.add_argument("--device-auth-public-key-file", default=os.environ.get("RE_V5_DEVICE_AUTH_PUBLIC_KEY_FILE", DEFAULT_DEVICE_AUTH_PUBLIC_KEY_FILE))
    parser.add_argument("--device-private-key-file", default=os.environ.get("RE_V5_DEVICE_PRIVATE_KEY_FILE", DEFAULT_DEVICE_PRIVATE_KEY_FILE))
    parser.add_argument("--device-public-key-file", default=os.environ.get("RE_V5_DEVICE_PUBLIC_KEY_FILE", DEFAULT_DEVICE_PUBLIC_KEY_FILE))
    parser.add_argument("--private-mode", choices=("auto", "always", "skip"), default=os.environ.get("RE_V5_DRIVE_PROFILE_PRIVATE_MODE", "auto"))
    parser.add_argument("--timeout", type=float, default=float(os.environ.get("RE_V5_DRIVE_PROFILE_DOWNLOAD_TIMEOUT", "20")))
    parser.add_argument("--request", default="")
    return parser.parse_args(argv)

