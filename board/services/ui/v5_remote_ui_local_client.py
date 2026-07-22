#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import hmac
import ipaddress
import json
import ssl
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Iterable, Mapping

from v5_remote_ui_auth import (
    AUTH_PROTOCOL,
    SESSION_SCHEME,
    canonical_auth_message,
    load_client_credentials,
    normalize_scopes,
    _base64url,
)


def _require_loopback_https(base_url: str) -> str:
    parsed = urllib.parse.urlparse(base_url)
    if parsed.scheme != "https" or not parsed.hostname:
        raise ValueError("local relay URL must use HTTPS")
    try:
        if not ipaddress.ip_address(parsed.hostname).is_loopback:
            raise ValueError("local relay URL must target loopback")
    except ValueError as exc:
        raise ValueError("local relay URL must target a loopback IP") from exc
    return base_url.rstrip("/") + "/"


def _json_request(
    opener,
    url: str,
    method: str = "GET",
    payload: Mapping[str, object] = None,
    authorization: str = "",
) -> Mapping[str, object]:
    data = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        data = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        headers["Content-Type"] = "application/json"
    if authorization:
        headers["Authorization"] = authorization
    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    with opener.open(request, timeout=2.0) as response:
        body = json.load(response)
    if not isinstance(body, dict):
        raise ValueError("relay response is not an object")
    return body


def fetch_local_health(base_url: str, ca_cert: Path) -> Mapping[str, object]:
    base_url = _require_loopback_https(base_url)
    context = ssl.create_default_context(cafile=str(ca_cert))
    if hasattr(ssl, "TLSVersion"):
        context.minimum_version = ssl.TLSVersion.TLSv1_2
    opener = urllib.request.build_opener(urllib.request.HTTPSHandler(context=context))
    return _json_request(opener, urllib.parse.urljoin(base_url, "local/health"))


def fetch_authenticated_json(
    base_url: str,
    ca_cert: Path,
    clients_file: Path,
    client_id: str,
    requested_scopes: Iterable[str],
    path: str,
) -> Mapping[str, object]:
    base_url = _require_loopback_https(base_url)
    if not path.startswith("/") or path.startswith("//"):
        raise ValueError("relay path must be absolute")
    device_id, clients = load_client_credentials(clients_file)
    credential = clients.get(client_id)
    if credential is None or not credential.local_only:
        raise ValueError("local relay client is missing or not local_only")
    scopes = normalize_scopes(requested_scopes)
    if not set(scopes).issubset(credential.scopes):
        raise ValueError("local relay client lacks requested scopes")
    context = ssl.create_default_context(cafile=str(ca_cert))
    if hasattr(ssl, "TLSVersion"):
        context.minimum_version = ssl.TLSVersion.TLSv1_2
    opener = urllib.request.build_opener(urllib.request.HTTPSHandler(context=context))
    challenge = _json_request(
        opener,
        urllib.parse.urljoin(
            base_url,
            "remote/auth/challenge?client_id=" + urllib.parse.quote(client_id, safe=""),
        ),
    )
    if (
        challenge.get("schema") != "v5.remote_auth_challenge.v1" or
        challenge.get("protocol") != AUTH_PROTOCOL or
        challenge.get("device_id") != device_id
    ):
        raise ValueError("relay challenge identity is invalid")
    challenge_id = str(challenge.get("challenge_id") or "")
    nonce = str(challenge.get("nonce") or "")
    mac = _base64url(hmac.new(
        credential.secret,
        canonical_auth_message(client_id, challenge_id, nonce, device_id, scopes),
        hashlib.sha256,
    ).digest())
    session = _json_request(
        opener,
        urllib.parse.urljoin(base_url, "remote/auth/session"),
        method="POST",
        payload={
            "schema": "v5.remote_auth_session_request.v1",
            "protocol": AUTH_PROTOCOL,
            "client_id": client_id,
            "challenge_id": challenge_id,
            "nonce": nonce,
            "requested_scopes": list(scopes),
            "mac": mac,
        },
    )
    if (
        session.get("schema") != "v5.remote_auth_session.v1" or
        session.get("protocol") != AUTH_PROTOCOL or
        session.get("device_id") != device_id or
        session.get("client_id") != client_id
    ):
        raise ValueError("relay session identity is invalid")
    token = str(session.get("session_token") or "")
    if not token:
        raise ValueError("relay session token is missing")
    return _json_request(
        opener,
        urllib.parse.urljoin(base_url, path.lstrip("/")),
        authorization=SESSION_SCHEME + " " + token,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Authenticated loopback client for v5 remote relay startup checks.")
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--ca-cert", type=Path, required=True)
    parser.add_argument("--clients-file", type=Path)
    parser.add_argument("--client-id", default="local-bootstrap")
    parser.add_argument("--scope", action="append")
    parser.add_argument("--path")
    parser.add_argument("--health-only", action="store_true")
    args = parser.parse_args()
    if args.health_only:
        payload = fetch_local_health(args.base_url, args.ca_cert)
    else:
        if args.clients_file is None or not args.scope or not args.path:
            parser.error("authenticated mode requires --clients-file, --scope and --path")
        payload = fetch_authenticated_json(
            args.base_url,
            args.ca_cert,
            args.clients_file,
            args.client_id,
            args.scope,
            args.path,
        )
    print(json.dumps(payload, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
