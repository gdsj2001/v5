#!/usr/bin/env python3
"""Install one verified Remote Relay credential bundle on its registered board."""

from __future__ import annotations

import argparse
import base64
import hashlib
import ipaddress
import json
import os
import re
import subprocess
import tempfile
from pathlib import Path
from typing import Sequence

from generate_v5_remote_relay_credentials import generate_bundle, verify_bundle


DEVICE_ID_RE = re.compile(r"[0-9]{6}\Z")
HOST_FINGERPRINT_RE = re.compile(r"SHA256:[A-Za-z0-9+/]{43}\Z")
HASH_RE = re.compile(r"[0-9a-f]{64}\Z")
BOARD_FILES = ("server-cert.pem", "server-key.pem", "clients.json")
REGISTER_STATUS = "/opt/8ax/drive-profiles/device_register_status.json"
REMOTE_TARGET = "/etc/6x-cnc/remote-relay"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(64 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def host_key_fingerprint(key_base64: str) -> str:
    key = base64.b64decode(key_base64, validate=True)
    digest = base64.b64encode(hashlib.sha256(key).digest()).decode("ascii").rstrip("=")
    return "SHA256:" + digest


def select_pinned_host_key(scan_text: str, expected_fingerprint: str) -> str:
    if not HOST_FINGERPRINT_RE.fullmatch(expected_fingerprint):
        raise ValueError("host key fingerprint must use canonical SHA256:<base64> form")
    matches: list[str] = []
    for raw_line in scan_text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split()
        if len(fields) != 3 or not fields[1].startswith("ssh-"):
            continue
        try:
            fingerprint = host_key_fingerprint(fields[2])
        except Exception:
            continue
        if fingerprint == expected_fingerprint:
            matches.append(line)
    if len(matches) != 1:
        raise RuntimeError(
            "board host key scan did not contain exactly one key matching the independently "
            f"confirmed fingerprint (matches={len(matches)})"
        )
    return matches[0] + "\n"


def validate_bundle(root: Path, device_id: str, board_ip: str) -> dict:
    if not DEVICE_ID_RE.fullmatch(device_id):
        raise ValueError("device_id must contain exactly six decimal digits")
    address = ipaddress.ip_address(board_ip)
    if not isinstance(address, ipaddress.IPv4Address) or address.is_unspecified:
        raise ValueError("board_ip must be a concrete IPv4 address")
    resolved = root.resolve(strict=True)
    if resolved.name != "remote-relay-" + device_id:
        raise ValueError("credential directory name does not match device_id")
    board = resolved / "board"
    entries = sorted(item.name for item in board.iterdir())
    if entries != sorted(BOARD_FILES):
        raise ValueError("board credential directory contains an unexpected file set")
    for name in BOARD_FILES:
        path = board / name
        if path.is_symlink() or not path.is_file():
            raise ValueError(name + " must be a regular non-symlink file")
        if path.stat().st_size <= 0 or path.stat().st_size > 128 * 1024:
            raise ValueError(name + " has an invalid size")
    summary = verify_bundle(resolved, device_id, str(address))
    summary["credential_directory"] = str(resolved)
    summary["board_file_sha256"] = {name: sha256_file(board / name) for name in BOARD_FILES}
    return summary


def run(command: Sequence[str], *, timeout: float, input_text: str | None = None) -> str:
    completed = subprocess.run(
        list(command),
        input=input_text,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
        check=False,
    )
    if completed.returncode != 0:
        tail = completed.stdout[-2000:].replace("\r", " ").replace("\n", " | ")
        raise RuntimeError(f"command failed rc={completed.returncode}: {command[0]}: {tail}")
    return completed.stdout


def ssh_options(board_ip: str, port: int, identity_file: Path, known_hosts: Path) -> list[str]:
    null_hosts = "NUL" if os.name == "nt" else "/dev/null"
    return [
        "-o", "BatchMode=yes",
        "-o", "ConnectTimeout=5",
        "-o", "StrictHostKeyChecking=yes",
        "-o", "UserKnownHostsFile=" + str(known_hosts),
        "-o", "GlobalKnownHostsFile=" + null_hosts,
        "-o", "HostKeyAlgorithms=+ssh-rsa",
        "-o", "PubkeyAcceptedAlgorithms=+ssh-rsa",
        "-i", str(identity_file),
        "-p", str(port),
        "root@" + board_ip,
    ]


def remote_registered_device_id(ssh_base: Sequence[str]) -> str:
    code = (
        "import json,re;"
        f"p=json.load(open('{REGISTER_STATUS}',encoding='utf-8-sig'));"
        "v=str(p.get('vpsDistributionId',''));"
        "print(v if re.fullmatch(r'[0-9]{6}',v) else '')"
    )
    value = run(["ssh", *ssh_base, "python3 -c \"" + code + "\""], timeout=10).strip()
    if not DEVICE_ID_RE.fullmatch(value):
        raise RuntimeError("board has no canonical registered six-digit device ID")
    return value


def install_script() -> str:
    return r'''#!/bin/sh
set -eu

stage=$1
expected_device_id=$2
cert_sha=$3
key_sha=$4
clients_sha=$5
register_status=/opt/8ax/drive-profiles/device_register_status.json
target=/etc/6x-cnc/remote-relay

[ "$(id -u)" = 0 ] || { echo "credential install requires root" >&2; exit 20; }
actual_device_id=$(python3 -c "import json,re;p=json.load(open('$register_status',encoding='utf-8-sig'));v=str(p.get('vpsDistributionId',''));print(v if re.fullmatch(r'[0-9]{6}',v) else '')")
[ "$actual_device_id" = "$expected_device_id" ] || {
  echo "registered board device ID mismatch" >&2
  exit 21
}
for name in server-cert.pem server-key.pem clients.json; do
  [ -f "$stage/$name" ] && [ ! -L "$stage/$name" ] || {
    echo "invalid staged credential file: $name" >&2
    exit 22
  }
done
check_hash() {
  expected=$1
  path=$2
  actual=$(sha256sum "$path" | awk '{print $1}')
  [ "$actual" = "$expected" ] || {
    echo "credential staging hash mismatch" >&2
    exit 23
  }
}
check_hash "$cert_sha" "$stage/server-cert.pem"
check_hash "$key_sha" "$stage/server-key.pem"
check_hash "$clients_sha" "$stage/clients.json"

install -d -m 0700 "$target"
temp=$(mktemp -d "$target/.install.XXXXXX")
trap 'rm -rf "$temp"' EXIT HUP INT TERM
install -m 0644 "$stage/server-cert.pem" "$temp/server-cert.pem"
install -m 0600 "$stage/server-key.pem" "$temp/server-key.pem"
install -m 0600 "$stage/clients.json" "$temp/clients.json"
check_hash "$cert_sha" "$temp/server-cert.pem"
check_hash "$key_sha" "$temp/server-key.pem"
check_hash "$clients_sha" "$temp/clients.json"
chown root:root "$temp/server-cert.pem" "$temp/server-key.pem" "$temp/clients.json"
mv -f "$temp/server-cert.pem" "$target/server-cert.pem"
mv -f "$temp/server-key.pem" "$target/server-key.pem"
mv -f "$temp/clients.json" "$target/clients.json"
sync "$target"
printf 'installed_device_id=%s\n' "$actual_device_id"
sha256sum "$target/server-cert.pem" "$target/server-key.pem" "$target/clients.json"
stat -c '%a %U:%G %n' "$target/server-cert.pem" "$target/server-key.pem" "$target/clients.json"
'''


def deploy(
    root: Path,
    summary: dict,
    board_ip: str,
    port: int,
    identity_file: Path,
    expected_host_key: str,
) -> dict:
    identity = identity_file.resolve(strict=True)
    if identity.is_symlink() or not identity.is_file():
        raise ValueError("SSH identity must be a regular non-symlink file")
    scan = run(["ssh-keyscan", "-p", str(port), board_ip], timeout=10)
    pinned_line = select_pinned_host_key(scan, expected_host_key)
    device_id = str(summary["device_id"])
    hashes = dict(summary["board_file_sha256"])
    if any(not HASH_RE.fullmatch(str(value)) for value in hashes.values()):
        raise ValueError("credential bundle hash set is invalid")

    with tempfile.TemporaryDirectory(prefix="v5-relay-credential-deploy-") as temporary:
        scratch = Path(temporary)
        known_hosts = scratch / "known_hosts"
        known_hosts.write_text(pinned_line, encoding="ascii", newline="\n")
        installer = scratch / "install.sh"
        installer.write_text(install_script(), encoding="utf-8", newline="\n")
        ssh_base = ssh_options(board_ip, port, identity, known_hosts)
        actual_device_id = remote_registered_device_id(ssh_base)
        if actual_device_id != device_id:
            raise RuntimeError(
                f"credential device ID {device_id} does not match registered board {actual_device_id}"
            )
        stage = f"/tmp/v5_remote_relay_credentials_{os.getpid()}"
        run(["ssh", *ssh_base, f"rm -rf '{stage}' && install -d -m 0700 '{stage}'"], timeout=10)
        try:
            scp_options = list(ssh_base[:-1])
            port_index = scp_options.index("-p")
            scp_options[port_index] = "-P"
            sources = [str(root.resolve() / "board" / name) for name in BOARD_FILES]
            sources.append(str(installer))
            run(["scp", "-O", *scp_options, *sources, ssh_base[-1] + ":" + stage + "/"], timeout=30)
            args = [
                "ssh", *ssh_base,
                f"chmod 0700 '{stage}/install.sh' && '{stage}/install.sh' '{stage}' "
                f"'{device_id}' '{hashes['server-cert.pem']}' '{hashes['server-key.pem']}' "
                f"'{hashes['clients.json']}'",
            ]
            readback = run(args, timeout=30)
        finally:
            run(["ssh", *ssh_base, f"rm -rf '{stage}'"], timeout=10)
    return {
        "ok": True,
        "device_id": device_id,
        "board_ip": board_ip,
        "host_key_sha256": expected_host_key,
        "readback": [line for line in readback.splitlines() if line.strip()],
    }


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="v5-relay-deploy-smoke-") as directory:
        root = Path(directory)
        bundle, _ = generate_bundle(root, "359764", "192.168.1.221")
        summary = validate_bundle(bundle, "359764", "192.168.1.221")
        if set(summary["board_file_sha256"]) != set(BOARD_FILES):
            raise AssertionError(summary)
        key = base64.b64encode(b"host-key-smoke").decode("ascii")
        fingerprint = host_key_fingerprint(key)
        selected = select_pinned_host_key("192.168.1.221 ssh-rsa " + key, fingerprint)
        if not selected.endswith("\n") or "ssh-rsa" not in selected:
            raise AssertionError(selected)
        script = install_script()
        if "device ID mismatch" not in script or "sha256sum" not in script:
            raise AssertionError("installer preconditions missing")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify and install device-specific Remote Relay credentials.",
    )
    parser.add_argument("--credential-directory", type=Path)
    parser.add_argument("--device-id")
    parser.add_argument("--board-ip")
    parser.add_argument("--board-port", type=int, default=22)
    parser.add_argument(
        "--identity-file",
        type=Path,
        default=Path(r"D:\授权私钥\remote-ssh-board-root.pem"),
    )
    parser.add_argument("--host-key-sha256")
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("deploy_v5_remote_relay_credentials self-test PASS")
        return 0
    if args.credential_directory is None or args.device_id is None or args.board_ip is None:
        parser.error("credential verification requires --credential-directory, --device-id and --board-ip")
    summary = validate_bundle(args.credential_directory, args.device_id, args.board_ip)
    if not args.apply:
        print(json.dumps({"ok": True, "apply": False, **summary}, sort_keys=True))
        return 0
    if not args.host_key_sha256:
        parser.error("--apply requires an independently confirmed --host-key-sha256")
    result = deploy(
        args.credential_directory,
        summary,
        args.board_ip,
        args.board_port,
        args.identity_file,
        args.host_key_sha256,
    )
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
