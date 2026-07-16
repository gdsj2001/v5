#!/usr/bin/env python3
"""Maintain one authorized reverse SSH tunnel from the board to the VPS."""

from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


SOURCE_AUTH_DIR = Path(__file__).resolve().parents[1] / "auth_download"
RUNTIME_AUTH_DIR = Path("/usr/libexec/8ax/auth_download")
for candidate in (RUNTIME_AUTH_DIR, SOURCE_AUTH_DIR):
    if candidate.is_dir() and str(candidate) not in sys.path:
        sys.path.insert(0, str(candidate))

from drive_profile_download_auth import (  # noqa: E402
    DeviceRequestSigner,
    compact_device_authorization,
    read_verified_device_authorization,
    resolve_board_dna,
)
from drive_profile_download_core import b64url_encode  # noqa: E402
from drive_profile_download_ipv4 import install_ipv4_urlopen_resolution  # noqa: E402


STATUS_SCHEMA = "v5.remote_ssh_tunnel_status.v1"
REMOTE_PERMISSION = "remote_ssh_tunnel"
PORT_MIN = 25000
PORT_MAX = 44999
STOP_REQUESTED = False


class RemoteSshTunnelError(RuntimeError):
    pass


def now_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def atomic_write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=str(path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temp_name, path)
    finally:
        try:
            os.unlink(temp_name)
        except FileNotFoundError:
            pass


def write_status(args: argparse.Namespace, state: str, **fields: Any) -> None:
    payload = {
        "schema": STATUS_SCHEMA,
        "state": state,
        "updated_at": now_utc(),
    }
    payload.update(fields)
    atomic_write_json(Path(args.status_file), payload)


def load_endpoints(path: str) -> tuple[list[str], dict[str, Any]]:
    try:
        payload = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    except Exception as exc:
        raise RemoteSshTunnelError("remote SSH endpoint configuration is unreadable") from exc
    api_urls = payload.get("api_base_urls")
    if not isinstance(api_urls, list):
        raise RemoteSshTunnelError("api_base_urls must be a list")
    clean_api_urls = [str(value).strip().rstrip("/") for value in api_urls if str(value).startswith("https://")]
    remote = payload.get("remote_ssh")
    if not clean_api_urls or not isinstance(remote, dict):
        raise RemoteSshTunnelError("remote SSH endpoint configuration is incomplete")
    host = str(remote.get("host") or "").strip()
    connect_host = str(remote.get("connect_host") or host).strip()
    user = str(remote.get("user") or "").strip()
    port = int(remote.get("port") or 0)
    if not host or not connect_host or not user or port != 22:
        raise RemoteSshTunnelError("remote SSH endpoint identity is invalid")
    return clean_api_urls, {
        "host": host,
        "connect_host": connect_host,
        "port": port,
        "user": user,
    }


def require_remote_permission(payload: dict[str, Any]) -> None:
    permissions = payload.get("permissions")
    if not isinstance(permissions, list) or REMOTE_PERMISSION not in [str(value) for value in permissions]:
        raise RemoteSshTunnelError("device authorization does not allow remote SSH")


def registration_from_response(payload: dict[str, Any], device_id: str, endpoint: dict[str, Any]) -> dict[str, Any]:
    if payload.get("success") is not True:
        raise RemoteSshTunnelError(str(payload.get("message") or "VPS rejected remote SSH registration"))
    assigned_port = int(payload.get("assignedPort") or 0)
    if (
        str(payload.get("deviceId") or "") != device_id
        or assigned_port < PORT_MIN
        or assigned_port > PORT_MAX
        or str(payload.get("vpsHost") or "") != endpoint["host"]
        or int(payload.get("vpsPort") or 0) != endpoint["port"]
        or str(payload.get("tunnelUser") or "") != endpoint["user"]
    ):
        raise RemoteSshTunnelError("VPS returned a mismatched remote SSH identity")
    return {"device_id": device_id, "assigned_port": assigned_port, **endpoint}


def register_tunnel(
    args: argparse.Namespace,
    api_urls: list[str],
    endpoint: dict[str, Any],
    dna: str,
    auth_envelope: str,
    auth_payload: dict[str, Any],
) -> dict[str, Any]:
    signer = DeviceRequestSigner(args, dna, auth_payload)
    body = json.dumps({"deviceId": signer.device_id}, separators=(",", ":")).encode("utf-8")
    last_error = "no VPS endpoint attempted"
    for base_url in api_urls:
        url = base_url + "/api/v1/device/remote-ssh/register"
        try:
            headers = signer.headers_for_url(url, method="POST", purpose=REMOTE_PERMISSION)
            headers.update({
                "Accept": "application/json",
                "Content-Type": "application/json; charset=utf-8",
                "User-Agent": "8ax-v5-remote-ssh/1",
                "X-8AX-Device-DNA": dna,
                "X-8AX-Device-Authorization": b64url_encode(auth_envelope.encode("utf-8")),
            })
            request = urllib.request.Request(url, data=body, headers=headers, method="POST")
            with urllib.request.urlopen(request, timeout=args.timeout) as response:
                result = json.loads(response.read().decode("utf-8"))
            return registration_from_response(result, signer.device_id, endpoint)
        except urllib.error.HTTPError as exc:
            try:
                error_payload = json.loads(exc.read().decode("utf-8", errors="replace"))
                last_error = str(error_payload.get("message") or error_payload.get("status") or "HTTP %d" % exc.code)
            except Exception:
                last_error = "HTTP %d" % exc.code
        except Exception as exc:
            last_error = str(exc)
    raise RemoteSshTunnelError("remote SSH registration failed: " + last_error)


def build_ssh_command(args: argparse.Namespace, registration: dict[str, Any]) -> list[str]:
    if not Path(args.known_hosts_file).is_file():
        raise RemoteSshTunnelError("pinned VPS SSH host key is missing")
    if not Path(args.device_private_key_file).is_file():
        raise RemoteSshTunnelError("device private key is missing")
    return [
        args.ssh_binary,
        "-NT",
        "-o", "BatchMode=yes",
        "-o", "ExitOnForwardFailure=yes",
        "-o", "ServerAliveInterval=30",
        "-o", "ServerAliveCountMax=3",
        "-o", "StrictHostKeyChecking=yes",
        "-o", "UserKnownHostsFile=" + args.known_hosts_file,
        "-o", "HostKeyAlias=" + registration["host"],
        "-i", args.device_private_key_file,
        "-p", str(registration["port"]),
        "-R", "127.0.0.1:%d:127.0.0.1:22" % registration["assigned_port"],
        "%s@%s" % (registration["user"], registration["connect_host"]),
    ]


def stop_requested(_signum: int, _frame: Any) -> None:
    global STOP_REQUESTED
    STOP_REQUESTED = True


def run(args: argparse.Namespace) -> int:
    signal.signal(signal.SIGTERM, stop_requested)
    signal.signal(signal.SIGINT, stop_requested)
    install_ipv4_urlopen_resolution()
    delay = max(1.0, args.retry_initial)
    while not STOP_REQUESTED:
        process: subprocess.Popen[bytes] | None = None
        try:
            api_urls, endpoint = load_endpoints(args.vps_endpoints_config)
            dna = str(resolve_board_dna(args).get("value") or "").strip()
            if not dna:
                raise RemoteSshTunnelError("live device DNA is unavailable")
            auth_envelope, auth_payload = read_verified_device_authorization(args, dna)
            require_remote_permission(auth_payload)
            registration = register_tunnel(args, api_urls, endpoint, dna, auth_envelope, auth_payload)
            command = build_ssh_command(args, registration)
            process = subprocess.Popen(command, stdin=subprocess.DEVNULL)
            write_status(
                args,
                "connecting",
                device_id=registration["device_id"],
                assigned_port=registration["assigned_port"],
                vps_host=registration["host"],
                process_id=process.pid,
            )
            started = time.monotonic()
            while not STOP_REQUESTED and process.poll() is None:
                if time.monotonic() - started >= 2.0:
                    write_status(
                        args,
                        "online",
                        device_id=registration["device_id"],
                        assigned_port=registration["assigned_port"],
                        vps_host=registration["host"],
                        process_id=process.pid,
                    )
                    delay = max(1.0, args.retry_initial)
                    started = float("inf")
                time.sleep(0.5)
            if STOP_REQUESTED:
                break
            code = process.returncode if process.returncode is not None else -1
            raise RemoteSshTunnelError("SSH tunnel exited with status %d" % code)
        except Exception as exc:
            write_status(args, "retry_wait", error=str(exc)[:300], retry_seconds=delay)
        finally:
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=2)
        deadline = time.monotonic() + delay
        while not STOP_REQUESTED and time.monotonic() < deadline:
            time.sleep(0.25)
        delay = min(args.retry_max, delay * 2.0)
    write_status(args, "stopped")
    return 0


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vps-endpoints-config", default="/etc/6x-cnc/vps_endpoints.json")
    parser.add_argument("--register-status-path", default="/opt/8ax/drive-profiles/device_register_status.json")
    parser.add_argument("--device-auth-file", default="/etc/6x-cnc/device_authorization.json")
    parser.add_argument("--device-auth-public-key-file", default="/etc/6x-cnc/device_auth_public.pem")
    parser.add_argument("--device-private-key-file", default="/etc/6x-cnc/device_private_key.pem")
    parser.add_argument("--device-public-key-file", default="/etc/6x-cnc/device_public_key.pem")
    parser.add_argument("--known-hosts-file", default="/etc/6x-cnc/vps_remote_ssh_known_hosts")
    parser.add_argument("--status-file", default="/run/8ax_v5_remote_ssh/status.json")
    parser.add_argument("--ssh-binary", default="/usr/bin/ssh")
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--retry-initial", type=float, default=5.0)
    parser.add_argument("--retry-max", type=float, default=300.0)
    parser.add_argument("--device-dna", default="")
    return parser.parse_args(argv)


if __name__ == "__main__":
    raise SystemExit(run(parse_args()))
