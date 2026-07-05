from __future__ import annotations

import os
import socket
from typing import Any

_ORIGINAL_GETADDRINFO = socket.getaddrinfo
_IPV4_RESOLUTION_INSTALLED = False


def force_ipv4_enabled() -> bool:
    value = os.environ.get("RE_V5_DRIVE_PROFILE_FORCE_IPV4", os.environ.get("AX8_DRIVE_PROFILE_FORCE_IPV4", "1"))
    return str(value).strip().lower() not in ("0", "false", "no", "off")

def install_ipv4_urlopen_resolution() -> None:
    global _IPV4_RESOLUTION_INSTALLED

    if _IPV4_RESOLUTION_INSTALLED or not force_ipv4_enabled():
        return

    def ipv4_getaddrinfo(host: str, port: Any, family: int = 0, type: int = 0, proto: int = 0, flags: int = 0) -> Any:
        if family in (0, socket.AF_UNSPEC, socket.AF_INET):
            return _ORIGINAL_GETADDRINFO(host, port, socket.AF_INET, type, proto, flags)
        return _ORIGINAL_GETADDRINFO(host, port, family, type, proto, flags)

    socket.getaddrinfo = ipv4_getaddrinfo
    _IPV4_RESOLUTION_INSTALLED = True
