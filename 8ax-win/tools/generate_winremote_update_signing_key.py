#!/usr/bin/env python3
from __future__ import annotations

import argparse
import getpass
import hashlib
import os
import subprocess
import tempfile
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec


def write_exclusive(path: Path, payload: bytes, mode: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_BINARY"):
        flags |= os.O_BINARY
    descriptor = os.open(path, flags, mode)
    try:
        os.write(descriptor, payload)
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    path.chmod(mode)


def protect_windows_private_file(path: Path) -> None:
    if os.name != "nt":
        return
    principal = subprocess.run(
        ["whoami"], check=True, capture_output=True, text=True,
    ).stdout.strip() or getpass.getuser()
    subprocess.run([
        "icacls", str(path),
        "/inheritance:r",
        "/grant:r", principal + ":F", "*S-1-5-18:F",
        "/remove:g", "*S-1-1-0", "*S-1-5-11", "*S-1-5-32-544", "*S-1-5-32-545",
        "/Q",
    ], check=True, capture_output=True, text=True)
    path.read_bytes()


def load_pair(private_path: Path, public_path: Path):
    private_key = serialization.load_pem_private_key(private_path.read_bytes(), password=None)
    public_key = serialization.load_pem_public_key(public_path.read_bytes())
    if not isinstance(private_key, ec.EllipticCurvePrivateKey):
        raise ValueError("update signing private key is not ECDSA")
    if not isinstance(public_key, ec.EllipticCurvePublicKey):
        raise ValueError("update signing public key is not ECDSA")
    if private_key.curve.name != "secp256r1" or public_key.curve.name != "secp256r1":
        raise ValueError("update signing key is not P-256")
    if private_key.public_key().public_numbers() != public_key.public_numbers():
        raise ValueError("update signing private/public key mismatch")
    return private_key, public_key


def generate(private_path: Path, public_path: Path) -> str:
    if private_path.exists() or public_path.exists():
        raise FileExistsError("refusing to overwrite an existing update signing key")
    key = ec.generate_private_key(ec.SECP256R1())
    private_pem = key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    )
    public_pem = key.public_key().public_bytes(
        serialization.Encoding.PEM,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    write_exclusive(private_path, private_pem, 0o600)
    try:
        protect_windows_private_file(private_path)
        write_exclusive(public_path, public_pem, 0o644)
        load_pair(private_path, public_path)
    except BaseException:
        if public_path.exists():
            public_path.unlink()
        if private_path.exists():
            private_path.unlink()
        raise
    return hashlib.sha256(public_pem).hexdigest()


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="v5-winremote-update-key-") as directory:
        root = Path(directory)
        private_path = root / "private.pem"
        public_path = root / "public.pem"
        fingerprint = generate(private_path, public_path)
        if len(fingerprint) != 64:
            raise AssertionError("public key fingerprint length")
        load_pair(private_path, public_path)
        try:
            generate(private_path, public_path)
        except FileExistsError:
            pass
        else:
            raise AssertionError("existing update signing key was overwritten")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate the offline WinRemote ECDSA P-256 update signing key.")
    parser.add_argument("--private-key", type=Path)
    parser.add_argument("--public-key", type=Path)
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("generate_winremote_update_signing_key self-test PASS")
        return 0
    if args.private_key is None or args.public_key is None:
        parser.error("--private-key and --public-key are required")
    if args.verify:
        load_pair(args.private_key, args.public_key)
        print("winremote update signing key verification PASS")
        return 0
    fingerprint = generate(args.private_key, args.public_key)
    print("generated WinRemote update signing key; public_pem_sha256=" + fingerprint)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
