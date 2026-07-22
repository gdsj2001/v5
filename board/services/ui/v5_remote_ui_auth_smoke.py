#!/usr/bin/env python3
from __future__ import annotations

import base64
import hashlib
import hmac
import json
import os
import tempfile
from pathlib import Path

from v5_remote_ui_auth import (
    AUTH_PROTOCOL,
    AuthError,
    AuthStore,
    SESSION_SCHEME,
    _base64url,
    canonical_auth_message,
)


DEVICE_ID = "359764"
CLIENT_ID = "winremote-359764"
SECRET = bytes(range(32))


def expect_auth_error(expected_status, expected_code, action):
    try:
        action()
    except AuthError as exc:
        if exc.status != expected_status or exc.code != expected_code:
            raise AssertionError((exc.status, exc.code))
        return
    raise AssertionError("expected AuthError")


def write_clients(path: Path) -> None:
    path.write_text(json.dumps({
        "schema": "v5.remote_relay_clients.v1",
        "device_id": DEVICE_ID,
        "clients": [
            {
                "client_id": CLIENT_ID,
                "secret_base64": base64.b64encode(SECRET).decode("ascii"),
                "scopes": ["viewer", "operator"],
                "local_only": False,
            },
            {
                "client_id": "local-bootstrap",
                "secret_base64": base64.b64encode(bytes(reversed(range(32)))).decode("ascii"),
                "scopes": ["viewer", "diagnostics"],
                "local_only": True,
            },
        ],
    }), encoding="utf-8")
    os.chmod(path, 0o600)


def create_session(store: AuthStore, peer: str, scopes):
    challenge = store.issue_challenge(CLIENT_ID, peer)
    message = canonical_auth_message(
        CLIENT_ID,
        challenge["challenge_id"],
        challenge["nonce"],
        DEVICE_ID,
        scopes,
    )
    mac = _base64url(hmac.new(SECRET, message, hashlib.sha256).digest())
    return challenge, store.create_session(
        CLIENT_ID,
        peer,
        challenge["challenge_id"],
        challenge["nonce"],
        scopes,
        mac,
    )


def main() -> int:
    now = [100.0]
    with tempfile.TemporaryDirectory(prefix="v5-remote-auth-") as temp_dir:
        clients_path = Path(temp_dir) / "clients.json"
        write_clients(clients_path)
        store = AuthStore.from_file(clients_path, clock=lambda: now[0])
        if store.device_id != DEVICE_ID:
            raise AssertionError(store.device_id)

        challenge, session_payload = create_session(store, "192.168.1.20", ["operator", "viewer"])
        if session_payload["protocol"] != AUTH_PROTOCOL:
            raise AssertionError(session_payload)
        authorization = SESSION_SCHEME + " " + session_payload["session_token"]
        viewer = store.authorize(authorization, "192.168.1.20", "viewer")
        if viewer.client_id != CLIENT_ID or viewer.scopes != frozenset({"operator", "viewer"}):
            raise AssertionError(viewer)
        expect_auth_error(403, "remote_scope_denied", lambda: store.authorize(
            authorization, "192.168.1.20", "program_manager"))
        expect_auth_error(401, "remote_auth_required", lambda: store.authorize(
            "", "192.168.1.20", "viewer"))
        expect_auth_error(401, "remote_session_invalid", lambda: store.authorize(
            authorization, "192.168.1.21", "viewer"))

        expect_auth_error(401, "remote_auth_failed", lambda: store.create_session(
            CLIENT_ID,
            "192.168.1.20",
            challenge["challenge_id"],
            challenge["nonce"],
            ["viewer", "operator"],
            _base64url(hmac.new(SECRET, b"replay", hashlib.sha256).digest()),
        ))

        bad = store.issue_challenge(CLIENT_ID, "192.168.1.20")
        expect_auth_error(401, "remote_auth_failed", lambda: store.create_session(
            CLIENT_ID,
            "192.168.1.20",
            bad["challenge_id"],
            bad["nonce"],
            ["viewer"],
            _base64url(b"x" * 32),
        ))
        expect_auth_error(401, "remote_auth_failed", lambda: store.create_session(
            CLIENT_ID,
            "192.168.1.20",
            bad["challenge_id"],
            bad["nonce"],
            ["viewer"],
            _base64url(hmac.new(SECRET, b"unused", hashlib.sha256).digest()),
        ))

        expect_auth_error(401, "remote_auth_failed", lambda: store.issue_challenge(
            "local-bootstrap", "192.168.1.20"))
        local_challenge = store.issue_challenge("local-bootstrap", "127.0.0.1")
        if local_challenge["device_id"] != DEVICE_ID:
            raise AssertionError(local_challenge)

        now[0] += 601.0
        expect_auth_error(401, "remote_session_invalid", lambda: store.authorize(
            authorization, "192.168.1.20", "viewer"))
        if store.diagnostic_snapshot()["active_sessions"] != 0:
            raise AssertionError(store.diagnostic_snapshot())

        if os.name == "posix":
            os.chmod(clients_path, 0o644)
            try:
                AuthStore.from_file(clients_path)
            except ValueError as exc:
                if "permissions" not in str(exc):
                    raise
            else:
                raise AssertionError("world-readable credential file accepted")

    print("v5_remote_ui_auth_smoke PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
