#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import tempfile
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec


MAX_MANIFEST_BYTES = 64 * 1024
MAX_SIGNATURE_BYTES = 256


def load_public(path: Path) -> ec.EllipticCurvePublicKey:
    key = serialization.load_pem_public_key(path.read_bytes())
    if not isinstance(key, ec.EllipticCurvePublicKey) or key.curve.name != "secp256r1":
        raise ValueError("WinRemote update public key is not ECDSA P-256")
    return key


def load_private(path: Path) -> ec.EllipticCurvePrivateKey:
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or key.curve.name != "secp256r1":
        raise ValueError("WinRemote update private key is not ECDSA P-256")
    return key


def read_manifest(path: Path) -> bytes:
    raw = path.read_bytes()
    if not raw or len(raw) > MAX_MANIFEST_BYTES:
        raise ValueError("manifest byte length is invalid")
    if raw.startswith(b"\xef\xbb\xbf"):
        raise ValueError("manifest must be UTF-8 without BOM")
    raw.decode("utf-8", errors="strict")
    return raw


def write_exclusive(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_BINARY"):
        flags |= os.O_BINARY
    descriptor = os.open(path, flags, 0o644)
    try:
        os.write(descriptor, payload)
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def sign(private_path: Path, public_path: Path, manifest_path: Path, signature_path: Path) -> None:
    private_key = load_private(private_path)
    public_key = load_public(public_path)
    if private_key.public_key().public_numbers() != public_key.public_numbers():
        raise ValueError("WinRemote update signing private/public key mismatch")
    raw = read_manifest(manifest_path)
    signature = private_key.sign(raw, ec.ECDSA(hashes.SHA256()))
    public_key.verify(signature, raw, ec.ECDSA(hashes.SHA256()))
    write_exclusive(signature_path, signature)


def verify(public_path: Path, manifest_path: Path, signature_path: Path) -> None:
    public_key = load_public(public_path)
    raw = read_manifest(manifest_path)
    signature = signature_path.read_bytes()
    if not signature or len(signature) > MAX_SIGNATURE_BYTES:
        raise ValueError("manifest signature byte length is invalid")
    try:
        public_key.verify(signature, raw, ec.ECDSA(hashes.SHA256()))
    except InvalidSignature as exc:
        raise ValueError("manifest signature is invalid") from exc


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="v5-winremote-sign-") as directory:
        root = Path(directory)
        private = ec.generate_private_key(ec.SECP256R1())
        private_path = root / "private.pem"
        public_path = root / "public.pem"
        manifest_path = root / "manifest.json"
        signature_path = root / "manifest.sig"
        private_path.write_bytes(private.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        ))
        public_path.write_bytes(private.public_key().public_bytes(
            serialization.Encoding.PEM,
            serialization.PublicFormat.SubjectPublicKeyInfo,
        ))
        manifest_path.write_bytes(b'{"schema":"v5.winremote_update_manifest.v2"}')
        sign(private_path, public_path, manifest_path, signature_path)
        verify(public_path, manifest_path, signature_path)
        manifest_path.write_bytes(manifest_path.read_bytes() + b" ")
        try:
            verify(public_path, manifest_path, signature_path)
        except ValueError:
            pass
        else:
            raise AssertionError("tampered manifest signature was accepted")


def main() -> int:
    parser = argparse.ArgumentParser(description="Sign or verify raw WinRemote update manifest bytes.")
    parser.add_argument("operation", choices=("sign", "verify", "self-test"))
    parser.add_argument("--private-key", type=Path)
    parser.add_argument("--public-key", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--signature", type=Path)
    args = parser.parse_args()
    if args.operation == "self-test":
        self_test()
        print("sign_winremote_update_manifest self-test PASS")
        return 0
    if args.public_key is None or args.manifest is None or args.signature is None:
        parser.error("--public-key, --manifest and --signature are required")
    if args.operation == "sign":
        if args.private_key is None:
            parser.error("sign requires --private-key")
        sign(args.private_key, args.public_key, args.manifest, args.signature)
        print("WinRemote manifest ECDSA P-256 signature PASS")
    else:
        verify(args.public_key, args.manifest, args.signature)
        print("WinRemote manifest signature verification PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
