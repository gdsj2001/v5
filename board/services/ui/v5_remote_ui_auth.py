#!/usr/bin/env python3
from __future__ import annotations

import base64
import hashlib
import hmac
import ipaddress
import json
import os
import re
import secrets
import ssl
import stat
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, FrozenSet, Iterable, List, Mapping, Optional, Tuple


AUTH_PROTOCOL = "v5.remote.auth.v1"
CLIENT_FILE_SCHEMA = "v5.remote_relay_clients.v1"
SESSION_SCHEME = "V5Session"
CHALLENGE_TTL_SECONDS = 30.0
SESSION_TTL_SECONDS = 600.0
MAX_CHALLENGES = 128
MAX_SESSIONS = 128
ALL_SCOPES = frozenset({
    "viewer",
    "diagnostics",
    "program_manager",
    "operator",
    "ota_admin",
})
CLIENT_ID_RE = re.compile(r"[A-Za-z0-9._-]{1,64}\Z")
DEVICE_ID_RE = re.compile(r"[0-9]{6}\Z")


class AuthError(Exception):
    def __init__(self, status: int, code: str, message: str) -> None:
        super().__init__(message)
        self.status = int(status)
        self.code = str(code)
        self.message = str(message)


@dataclass(frozen=True)
class ClientCredential:
    client_id: str
    secret: bytes
    scopes: FrozenSet[str]
    local_only: bool


@dataclass(frozen=True)
class Challenge:
    challenge_id: str
    client_id: str
    peer: str
    nonce: str
    expires_monotonic: float


@dataclass(frozen=True)
class AuthSession:
    token_hash: str
    client_id: str
    peer: str
    scopes: FrozenSet[str]
    expires_monotonic: float


def _reject_duplicate_keys(pairs: List[Tuple[str, object]]) -> Dict[str, object]:
    result: Dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key: %s" % key)
        result[key] = value
    return result


def _read_private_json(path: Path) -> Mapping[str, object]:
    if path.is_symlink():
        raise ValueError("credential file must not be a symlink")
    info = path.stat()
    if not stat.S_ISREG(info.st_mode):
        raise ValueError("credential file is not regular")
    if os.name == "posix" and info.st_mode & 0o077:
        raise ValueError("credential file permissions must be 0600 or stricter")
    if hasattr(os, "geteuid") and os.geteuid() == 0 and info.st_uid != 0:
        raise ValueError("credential file must be owned by root")
    if info.st_size <= 0 or info.st_size > 65536:
        raise ValueError("credential file size is invalid")
    payload = json.loads(
        path.read_text(encoding="utf-8"),
        object_pairs_hook=_reject_duplicate_keys,
    )
    if not isinstance(payload, dict):
        raise ValueError("credential root must be an object")
    return payload


def _decode_secret(value: object) -> bytes:
    if not isinstance(value, str):
        raise ValueError("client secret is not text")
    try:
        decoded = base64.b64decode(value.encode("ascii"), validate=True)
    except (ValueError, UnicodeError) as exc:
        raise ValueError("client secret is not valid base64") from exc
    if len(decoded) != 32:
        raise ValueError("client secret must contain exactly 256 bits")
    return decoded


def normalize_scopes(values: Iterable[object]) -> Tuple[str, ...]:
    normalized = tuple(sorted(set(str(value) for value in values)))
    if not normalized or any(value not in ALL_SCOPES for value in normalized):
        raise ValueError("requested scopes are invalid")
    return normalized


def canonical_auth_message(
    client_id: str,
    challenge_id: str,
    nonce: str,
    device_id: str,
    requested_scopes: Iterable[object],
) -> bytes:
    scopes = normalize_scopes(requested_scopes)
    return (
        AUTH_PROTOCOL + "\n" +
        client_id + "\n" +
        challenge_id + "\n" +
        nonce + "\n" +
        device_id + "\n" +
        ",".join(scopes)
    ).encode("utf-8")


def _base64url(payload: bytes) -> str:
    return base64.urlsafe_b64encode(payload).decode("ascii").rstrip("=")


def _decode_base64url(value: str) -> bytes:
    if not value or not re.fullmatch(r"[A-Za-z0-9_-]+", value):
        raise ValueError("invalid base64url")
    padding = "=" * ((4 - len(value) % 4) % 4)
    return base64.urlsafe_b64decode((value + padding).encode("ascii"))


def _is_loopback(peer: str) -> bool:
    try:
        return ipaddress.ip_address(peer).is_loopback
    except ValueError:
        return False


def load_client_credentials(path: Path) -> Tuple[str, Dict[str, ClientCredential]]:
    payload = _read_private_json(path)
    if set(payload) != {"schema", "device_id", "clients"}:
        raise ValueError("credential file fields are invalid")
    if payload.get("schema") != CLIENT_FILE_SCHEMA:
        raise ValueError("credential file schema is invalid")
    device_id = str(payload.get("device_id") or "")
    if not DEVICE_ID_RE.fullmatch(device_id):
        raise ValueError("device_id must be exactly six decimal digits")
    raw_clients = payload.get("clients")
    if not isinstance(raw_clients, list) or not 1 <= len(raw_clients) <= 32:
        raise ValueError("clients must contain 1..32 entries")
    clients: Dict[str, ClientCredential] = {}
    for raw in raw_clients:
        if not isinstance(raw, dict) or set(raw) != {
            "client_id", "secret_base64", "scopes", "local_only"
        }:
            raise ValueError("client entry fields are invalid")
        client_id = str(raw.get("client_id") or "")
        if not CLIENT_ID_RE.fullmatch(client_id) or client_id in clients:
            raise ValueError("client_id is invalid or duplicated")
        raw_scopes = raw.get("scopes")
        if not isinstance(raw_scopes, list):
            raise ValueError("client scopes must be a list")
        scopes = frozenset(normalize_scopes(raw_scopes))
        local_only = raw.get("local_only")
        if not isinstance(local_only, bool):
            raise ValueError("client local_only must be boolean")
        clients[client_id] = ClientCredential(
            client_id=client_id,
            secret=_decode_secret(raw.get("secret_base64")),
            scopes=scopes,
            local_only=local_only,
        )
    return device_id, clients


def certificate_common_name(cert_path: Path) -> str:
    decoder = getattr(getattr(ssl, "_ssl", None), "_test_decode_cert", None)
    if decoder is None:
        raise ValueError("Python TLS certificate decoder is unavailable")
    decoded = decoder(str(cert_path))
    for relative_name in decoded.get("subject", ()):
        for key, value in relative_name:
            if key == "commonName":
                return str(value)
    raise ValueError("TLS certificate common name is missing")


def build_server_tls_context(cert_path: Path, key_path: Path, device_id: str) -> ssl.SSLContext:
    if key_path.is_symlink():
        raise ValueError("TLS private key must not be a symlink")
    key_info = key_path.stat()
    if not stat.S_ISREG(key_info.st_mode) or (
        os.name == "posix" and key_info.st_mode & 0o077
    ):
        raise ValueError("TLS private key permissions must be 0600 or stricter")
    if hasattr(os, "geteuid") and os.geteuid() == 0 and key_info.st_uid != 0:
        raise ValueError("TLS private key must be owned by root")
    expected_common_name = "8ax-device-" + device_id
    if certificate_common_name(cert_path) != expected_common_name:
        raise ValueError("TLS certificate device identity mismatch")
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls_version = getattr(ssl, "TLSVersion", None)
    if tls_version is not None:
        context.minimum_version = tls_version.TLSv1_2
    else:
        context.options |= getattr(ssl, "OP_NO_SSLv2", 0)
        context.options |= getattr(ssl, "OP_NO_SSLv3", 0)
        context.options |= getattr(ssl, "OP_NO_TLSv1", 0)
        context.options |= getattr(ssl, "OP_NO_TLSv1_1", 0)
    context.load_cert_chain(certfile=str(cert_path), keyfile=str(key_path))
    return context


class AuthStore:
    def __init__(
        self,
        device_id: str,
        clients: Mapping[str, ClientCredential],
        clock=time.monotonic,
    ) -> None:
        if not DEVICE_ID_RE.fullmatch(device_id):
            raise ValueError("device_id is invalid")
        if not clients:
            raise ValueError("at least one client is required")
        self.device_id = device_id
        self._clients = dict(clients)
        self._clock = clock
        self._lock = threading.Lock()
        self._challenges: Dict[str, Challenge] = {}
        self._sessions: Dict[str, AuthSession] = {}

    @classmethod
    def from_file(cls, path: Path, clock=time.monotonic) -> "AuthStore":
        device_id, clients = load_client_credentials(path)
        return cls(device_id, clients, clock=clock)

    def _purge_locked(self, now: float) -> None:
        self._challenges = {
            key: value for key, value in self._challenges.items()
            if value.expires_monotonic > now
        }
        self._sessions = {
            key: value for key, value in self._sessions.items()
            if value.expires_monotonic > now
        }

    def issue_challenge(self, client_id: str, peer: str) -> Mapping[str, object]:
        credential = self._clients.get(client_id)
        if credential is None or (credential.local_only and not _is_loopback(peer)):
            raise AuthError(401, "remote_auth_failed", "客户端认证失败。")
        now = self._clock()
        challenge = Challenge(
            challenge_id=_base64url(secrets.token_bytes(18)),
            client_id=client_id,
            peer=peer,
            nonce=_base64url(secrets.token_bytes(32)),
            expires_monotonic=now + CHALLENGE_TTL_SECONDS,
        )
        with self._lock:
            self._purge_locked(now)
            if len(self._challenges) >= MAX_CHALLENGES:
                oldest = min(self._challenges.values(), key=lambda item: item.expires_monotonic)
                self._challenges.pop(oldest.challenge_id, None)
            self._challenges[challenge.challenge_id] = challenge
        return {
            "schema": "v5.remote_auth_challenge.v1",
            "protocol": AUTH_PROTOCOL,
            "device_id": self.device_id,
            "challenge_id": challenge.challenge_id,
            "nonce": challenge.nonce,
            "expires_in_seconds": int(CHALLENGE_TTL_SECONDS),
        }

    def create_session(
        self,
        client_id: str,
        peer: str,
        challenge_id: str,
        nonce: str,
        requested_scopes: Iterable[object],
        mac_text: str,
    ) -> Mapping[str, object]:
        try:
            scopes = normalize_scopes(requested_scopes)
            submitted_mac = _decode_base64url(mac_text)
        except (TypeError, ValueError) as exc:
            raise AuthError(401, "remote_auth_failed", "客户端认证失败。") from exc
        if len(submitted_mac) != hashlib.sha256().digest_size:
            raise AuthError(401, "remote_auth_failed", "客户端认证失败。")
        now = self._clock()
        with self._lock:
            self._purge_locked(now)
            challenge = self._challenges.pop(challenge_id, None)
        credential = self._clients.get(client_id)
        if (
            challenge is None or credential is None or
            challenge.client_id != client_id or challenge.peer != peer or
            challenge.nonce != nonce or challenge.expires_monotonic <= now or
            (credential.local_only and not _is_loopback(peer)) or
            not set(scopes).issubset(credential.scopes)
        ):
            raise AuthError(401, "remote_auth_failed", "客户端认证失败。")
        expected_mac = hmac.new(
            credential.secret,
            canonical_auth_message(
                client_id,
                challenge_id,
                nonce,
                self.device_id,
                scopes,
            ),
            hashlib.sha256,
        ).digest()
        if not hmac.compare_digest(expected_mac, submitted_mac):
            raise AuthError(401, "remote_auth_failed", "客户端认证失败。")
        token = _base64url(secrets.token_bytes(32))
        token_hash = hashlib.sha256(token.encode("ascii")).hexdigest()
        session = AuthSession(
            token_hash=token_hash,
            client_id=client_id,
            peer=peer,
            scopes=frozenset(scopes),
            expires_monotonic=now + SESSION_TTL_SECONDS,
        )
        with self._lock:
            self._purge_locked(now)
            if len(self._sessions) >= MAX_SESSIONS:
                oldest = min(self._sessions.values(), key=lambda item: item.expires_monotonic)
                self._sessions.pop(oldest.token_hash, None)
            self._sessions[token_hash] = session
        return {
            "schema": "v5.remote_auth_session.v1",
            "protocol": AUTH_PROTOCOL,
            "device_id": self.device_id,
            "client_id": client_id,
            "session_token": token,
            "scopes": list(scopes),
            "expires_in_seconds": int(SESSION_TTL_SECONDS),
        }

    def authorize(self, authorization: Optional[str], peer: str, required_scope: str) -> AuthSession:
        if required_scope not in ALL_SCOPES:
            raise ValueError("required scope is invalid")
        parts = str(authorization or "").split(" ", 1)
        if len(parts) != 2 or parts[0] != SESSION_SCHEME or not parts[1]:
            raise AuthError(401, "remote_auth_required", "需要客户端认证。")
        token_hash = hashlib.sha256(parts[1].encode("utf-8")).hexdigest()
        now = self._clock()
        with self._lock:
            self._purge_locked(now)
            session = self._sessions.get(token_hash)
        if session is None or session.peer != peer:
            raise AuthError(401, "remote_session_invalid", "远程会话无效或已过期。")
        if required_scope not in session.scopes:
            raise AuthError(403, "remote_scope_denied", "当前客户端没有此操作权限。")
        return session

    def session_is_current(self, session: Optional[AuthSession]) -> bool:
        if session is None:
            return False
        now = self._clock()
        with self._lock:
            self._purge_locked(now)
            current = self._sessions.get(session.token_hash)
        return current == session and session.expires_monotonic > now

    def diagnostic_snapshot(self) -> Mapping[str, object]:
        now = self._clock()
        with self._lock:
            self._purge_locked(now)
            challenge_count = len(self._challenges)
            session_count = len(self._sessions)
        return {
            "protocol": AUTH_PROTOCOL,
            "device_id": self.device_id,
            "configured_clients": len(self._clients),
            "active_challenges": challenge_count,
            "active_sessions": session_count,
            "session_ttl_seconds": int(SESSION_TTL_SECONDS),
        }
