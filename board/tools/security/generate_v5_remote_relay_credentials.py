#!/usr/bin/env python3
from __future__ import annotations

import argparse
import base64
import datetime
import getpass
import hashlib
import ipaddress
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID


CLIENT_SCHEMA = "v5.remote_relay_clients.v1"
PROFILE_SCHEMA = "v5.winremote_relay_security.v1"
ALL_SCOPES = (
    "viewer",
    "diagnostics",
    "program_manager",
    "operator",
    "ota_admin",
)
DEVICE_ID_RE = re.compile(r"[0-9]{6}\Z")


def write_private(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_BINARY"):
        flags |= os.O_BINARY
    fd = os.open(path, flags, 0o600)
    try:
        os.write(fd, payload)
        os.fsync(fd)
    finally:
        os.close(fd)
    path.chmod(0o600)


def json_bytes(payload: dict) -> bytes:
    return (json.dumps(
        payload,
        ensure_ascii=False,
        sort_keys=True,
        indent=2,
    ) + "\n").encode("utf-8")


def protect_windows_tree(path: Path) -> None:
    if os.name != "nt":
        return
    whoami = subprocess.run(
        ["whoami"], check=True, capture_output=True, text=True,
    ).stdout.strip()
    principal = whoami or getpass.getuser()
    items = list(path.rglob("*"))
    files = [item for item in items if item.is_file()]
    directories = sorted(
        [item for item in items if item.is_dir()],
        key=lambda item: len(item.parts),
        reverse=True,
    )
    remove_accounts = (
        "*S-1-1-0",
        "*S-1-3-0",
        "*S-1-3-4",
        "*S-1-5-11",
        "*S-1-5-32-544",
        "*S-1-5-32-545",
    )

    def apply_acl(target: Path, inherited: bool) -> None:
        suffix = ":(OI)(CI)F" if inherited else ":F"
        command = [
            "icacls",
            str(target),
            "/inheritance:r",
            "/grant:r",
            principal + suffix,
            "*S-1-5-18" + suffix,
            "/remove:g",
            *remove_accounts,
            "/Q",
        ]
        subprocess.run(command, check=True, capture_output=True, text=True)

    # Apply file ACEs first: directory-only inheritance flags do not grant
    # access to an existing file whose inherited ACL was already removed.
    for item in files:
        apply_acl(item, inherited=False)
    for item in directories:
        apply_acl(item, inherited=True)
    apply_acl(path, inherited=True)
    for item in files:
        with item.open("rb") as stream:
            stream.read(1)


def create_certificate(device_id: str, board_ip: ipaddress._BaseAddress):
    server_name = "8ax-device-" + device_id
    key = ec.generate_private_key(ec.SECP256R1())
    now = datetime.datetime.now(datetime.timezone.utc)
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, server_name)])
    certificate = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(subject)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(minutes=5))
        .not_valid_after(now + datetime.timedelta(days=1825))
        .add_extension(
            x509.SubjectAlternativeName([
                x509.DNSName(server_name),
                x509.IPAddress(ipaddress.ip_address("127.0.0.1")),
                x509.IPAddress(board_ip),
            ]),
            critical=False,
        )
        .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
        .add_extension(
            x509.KeyUsage(
                digital_signature=True,
                content_commitment=False,
                key_encipherment=False,
                data_encipherment=False,
                key_agreement=False,
                key_cert_sign=True,
                crl_sign=True,
                encipher_only=False,
                decipher_only=False,
            ),
            critical=True,
        )
        .add_extension(
            x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]),
            critical=False,
        )
        .sign(key, hashes.SHA256())
    )
    return key, certificate


def verify_bundle(root: Path, device_id: str, board_ip: str) -> dict:
    board = root / "board"
    winremote = root / "winremote"
    cert_bytes = (board / "server-cert.pem").read_bytes()
    key_bytes = (board / "server-key.pem").read_bytes()
    certificate = x509.load_pem_x509_certificate(cert_bytes)
    private_key = serialization.load_pem_private_key(key_bytes, password=None)
    if not isinstance(private_key, ec.EllipticCurvePrivateKey):
        raise ValueError("relay private key is not ECDSA")
    if private_key.curve.name != "secp256r1":
        raise ValueError("relay private key is not P-256")
    if private_key.public_key().public_numbers() != certificate.public_key().public_numbers():
        raise ValueError("relay certificate/private key mismatch")
    common_name = certificate.subject.get_attributes_for_oid(NameOID.COMMON_NAME)
    if len(common_name) != 1 or common_name[0].value != "8ax-device-" + device_id:
        raise ValueError("relay certificate device identity mismatch")
    san = certificate.extensions.get_extension_for_class(x509.SubjectAlternativeName).value
    if ipaddress.ip_address(board_ip) not in san.get_values_for_type(x509.IPAddress):
        raise ValueError("relay certificate board IP SAN is missing")
    clients = json.loads((board / "clients.json").read_text(encoding="utf-8"))
    profile = json.loads((winremote / "relay-security.json").read_text(encoding="utf-8"))
    if clients.get("schema") != CLIENT_SCHEMA or clients.get("device_id") != device_id:
        raise ValueError("relay clients identity is invalid")
    entries = clients.get("clients")
    if not isinstance(entries, list) or len(entries) != 2:
        raise ValueError("relay clients must contain WinRemote and local-bootstrap")
    by_id = {str(item.get("client_id")): item for item in entries}
    if set(by_id) != {"winremote", "local-bootstrap"}:
        raise ValueError("relay client IDs are invalid")
    for client_id, item in by_id.items():
        secret = base64.b64decode(str(item.get("secret_base64") or ""), validate=True)
        if len(secret) != 32:
            raise ValueError(client_id + " secret is not 256 bits")
    fingerprint = hashlib.sha256(certificate.public_bytes(serialization.Encoding.DER)).hexdigest()
    if (
        profile.get("schema") != PROFILE_SCHEMA or
        profile.get("device_id") != device_id or
        profile.get("relay_base_uri") != "https://" + board_ip + ":18080/" or
        profile.get("certificate_sha256") != fingerprint or
        profile.get("client_id") != "winremote" or
        profile.get("client_secret_base64") != by_id["winremote"].get("secret_base64") or
        tuple(profile.get("scopes") or ()) != ALL_SCOPES
    ):
        raise ValueError("WinRemote relay security profile is inconsistent")
    return {
        "device_id": device_id,
        "relay_base_uri": profile["relay_base_uri"],
        "certificate_sha256": fingerprint,
        "certificate_not_after_utc": certificate.not_valid_after_utc.isoformat(),
    }


def generate_bundle(output_root: Path, device_id: str, board_ip_text: str) -> tuple[Path, dict]:
    if not DEVICE_ID_RE.fullmatch(device_id):
        raise ValueError("device_id must contain exactly six decimal digits")
    board_ip = ipaddress.ip_address(board_ip_text)
    if not isinstance(board_ip, ipaddress.IPv4Address) or board_ip.is_unspecified:
        raise ValueError("board_ip must be a concrete IPv4 address")
    destination = output_root.resolve() / ("remote-relay-" + device_id)
    if destination.exists():
        raise FileExistsError(
            "credential directory already exists; explicit rotation must use a new directory: " +
            str(destination)
        )
    output_root.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".remote-relay-", dir=str(output_root.resolve())))
    try:
        key, certificate = create_certificate(device_id, board_ip)
        certificate_pem = certificate.public_bytes(serialization.Encoding.PEM)
        private_key_pem = key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        )
        winremote_secret = os.urandom(32)
        local_secret = os.urandom(32)
        clients = {
            "schema": CLIENT_SCHEMA,
            "device_id": device_id,
            "clients": [
                {
                    "client_id": "winremote",
                    "secret_base64": base64.b64encode(winremote_secret).decode("ascii"),
                    "scopes": list(ALL_SCOPES),
                    "local_only": False,
                },
                {
                    "client_id": "local-bootstrap",
                    "secret_base64": base64.b64encode(local_secret).decode("ascii"),
                    "scopes": ["viewer", "diagnostics"],
                    "local_only": True,
                },
            ],
        }
        fingerprint = hashlib.sha256(
            certificate.public_bytes(serialization.Encoding.DER),
        ).hexdigest()
        profile = {
            "schema": PROFILE_SCHEMA,
            "device_id": device_id,
            "relay_base_uri": "https://" + str(board_ip) + ":18080/",
            "certificate_sha256": fingerprint,
            "client_id": "winremote",
            "client_secret_base64": base64.b64encode(winremote_secret).decode("ascii"),
            "scopes": list(ALL_SCOPES),
        }
        write_private(temporary / "board" / "server-cert.pem", certificate_pem)
        write_private(temporary / "board" / "server-key.pem", private_key_pem)
        write_private(temporary / "board" / "clients.json", json_bytes(clients))
        write_private(temporary / "winremote" / "relay-security.json", json_bytes(profile))
        summary = verify_bundle(temporary, device_id, str(board_ip))
        write_private(temporary / "public-summary.json", json_bytes({
            "schema": "v5.remote_relay_identity_summary.v1",
            **summary,
        }))
        protect_windows_tree(temporary)
        os.replace(temporary, destination)
        temporary = None
        return destination, summary
    finally:
        if temporary is not None and temporary.exists():
            shutil.rmtree(temporary)


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="v5-remote-credential-smoke-") as directory:
        destination, summary = generate_bundle(Path(directory), "359764", "192.168.1.221")
        if summary["device_id"] != "359764" or not destination.is_dir():
            raise AssertionError(summary)
        verify_bundle(destination, "359764", "192.168.1.221")
        try:
            generate_bundle(Path(directory), "359764", "192.168.1.221")
        except FileExistsError:
            pass
        else:
            raise AssertionError("credential generator overwrote an existing identity")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate one V5 remote relay TLS identity and scoped client credentials.",
    )
    parser.add_argument("--output-root", type=Path)
    parser.add_argument("--device-id")
    parser.add_argument("--board-ip")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("generate_v5_remote_relay_credentials self-test PASS")
        return 0
    if args.output_root is None or args.device_id is None or args.board_ip is None:
        parser.error("generation requires --output-root, --device-id and --board-ip")
    destination, summary = generate_bundle(
        args.output_root,
        args.device_id,
        args.board_ip,
    )
    print(json.dumps({
        "ok": True,
        "credential_directory": str(destination),
        **summary,
    }, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
