#!/usr/bin/env python3
from __future__ import annotations

import datetime
import ipaddress
import socket
import ssl
import tempfile
import threading
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

from v5_remote_ui_contract import PayloadViews
from v5_remote_ui_auth import build_server_tls_context
from v5_remote_ui_protocol import send_ws_frame


DEVICE_ID = "359764"
SERVER_NAME = "8ax-device-" + DEVICE_ID


def create_test_identity(root: Path) -> tuple[Path, Path]:
    key = ec.generate_private_key(ec.SECP256R1())
    now = datetime.datetime.now(datetime.timezone.utc)
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, SERVER_NAME)])
    certificate = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(subject)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(minutes=5))
        .not_valid_after(now + datetime.timedelta(days=1))
        .add_extension(
            x509.SubjectAlternativeName([
                x509.DNSName(SERVER_NAME),
                x509.IPAddress(ipaddress.ip_address("127.0.0.1")),
            ]),
            critical=False,
        )
        .add_extension(
            x509.BasicConstraints(ca=True, path_length=None),
            critical=True,
        )
        .sign(key, hashes.SHA256())
    )
    cert_path = root / "server-cert.pem"
    key_path = root / "server-key.pem"
    cert_path.write_bytes(certificate.public_bytes(serialization.Encoding.PEM))
    key_path.write_bytes(key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    ))
    key_path.chmod(0o600)
    return cert_path, key_path


def listen_with_context(context: ssl.SSLContext) -> tuple[ssl.SSLSocket, int]:
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", 0))
    listener.listen(2)
    port = int(listener.getsockname()[1])
    return context.wrap_socket(listener, server_side=True), port


def check_https_accept(cert_path: Path, context: ssl.SSLContext) -> None:
    listener, port = listen_with_context(context)
    result: dict[str, object] = {}

    def serve() -> None:
        try:
            connection, _peer = listener.accept()
            with connection:
                result["version"] = connection.version()
                request = connection.recv(4096)
                if not request.startswith(b"GET /local/health HTTP/1.1\r\n"):
                    raise AssertionError(request)
                connection.sendall(
                    b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK")
                send_ws_frame(connection, 0x1, PayloadViews([
                    memoryview(b"O"), memoryview(b"K"),
                ], 2))
        except BaseException as exc:  # surfaced in the test thread
            result["error"] = exc
        finally:
            listener.close()

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    client_context = ssl.create_default_context(cafile=str(cert_path))
    client_context.minimum_version = ssl.TLSVersion.TLSv1_2
    with socket.create_connection(("127.0.0.1", port), timeout=2.0) as raw:
        with client_context.wrap_socket(raw, server_hostname=SERVER_NAME) as client:
            client.sendall(b"GET /local/health HTTP/1.1\r\nHost: " + SERVER_NAME.encode() + b"\r\n\r\n")
            response_parts = []
            while True:
                chunk = client.recv(4096)
                if not chunk:
                    break
                response_parts.append(chunk)
            response = b"".join(response_parts)
    thread.join(2.0)
    if thread.is_alive() or "error" in result:
        raise AssertionError(result)
    if not response.startswith(b"HTTP/1.1 200 OK"):
        raise AssertionError(response)
    if not response.endswith(b"\x81\x02OK"):
        raise AssertionError(response)
    if result.get("version") not in {"TLSv1.2", "TLSv1.3"}:
        raise AssertionError(result)


def check_plaintext_rejected(context: ssl.SSLContext) -> None:
    listener, port = listen_with_context(context)
    result: dict[str, object] = {}

    def serve() -> None:
        try:
            connection, _peer = listener.accept()
            connection.close()
            result["accepted"] = True
        except ssl.SSLError:
            result["tls_rejected"] = True
        except BaseException as exc:
            result["error"] = exc
        finally:
            listener.close()

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    response = b""
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=2.0) as client:
            client.sendall(b"GET /remote/info HTTP/1.1\r\nHost: board\r\n\r\n")
            try:
                response = client.recv(4096)
            except (ConnectionError, OSError):
                pass
    finally:
        thread.join(2.0)
    if thread.is_alive() or result.get("accepted") or "error" in result:
        raise AssertionError(result)
    if not result.get("tls_rejected") or response.startswith(b"HTTP/"):
        raise AssertionError((result, response))


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="v5-remote-tls-") as directory:
        cert_path, key_path = create_test_identity(Path(directory))
        context = build_server_tls_context(cert_path, key_path, DEVICE_ID)
        check_https_accept(cert_path, context)
        check_plaintext_rejected(context)
        try:
            build_server_tls_context(cert_path, key_path, "000000")
        except ValueError as exc:
            if "identity mismatch" not in str(exc):
                raise
        else:
            raise AssertionError("certificate/device mismatch was accepted")
    print("v5_remote_ui_tls_smoke PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
