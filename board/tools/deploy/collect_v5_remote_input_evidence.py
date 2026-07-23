#!/usr/bin/env python3
import argparse
import base64
import hashlib
import hmac
import json
import os
import socket
import ssl
import struct
import subprocess
import time
import urllib.parse
import urllib.request
from pathlib import Path


AUTH_PROTOCOL = "v5.remote.auth.v1"
SESSION_SCHEME = "V5Session"
SECURITY_PROFILE_SCHEMA = "v5.winremote_relay_security.v1"


def ssh(host, command):
    return subprocess.run(["ssh", host, command], check=True, text=True, capture_output=True).stdout


def scp_from(host, remote, local):
    local_path = Path(local)
    with local_path.open("wb") as fp:
        subprocess.run(["ssh", host, f"cat {remote!r}"], check=True, stdout=fp)


def count_remote_lines(host, path):
    out = ssh(host, f"test -f {path!r} && wc -l < {path!r} || printf 0")
    return int(out.strip() or "0")


def fetch_delta(host, remote, before, local):
    cmd = (
        ": > /tmp/v5_remote_input_delta.tmp; "
        f"test -f {remote!r} && tail -n +{before + 1} {remote!r} >> /tmp/v5_remote_input_delta.tmp || :"
    )
    ssh(host, cmd)
    scp_from(host, "/tmp/v5_remote_input_delta.tmp", local)


def load_security_profile(path, ca_cert, host, port, required_scopes):
    profile_path = Path(path)
    ca_path = Path(ca_cert)
    if not profile_path.is_absolute() or not ca_path.is_absolute():
        raise ValueError("relay security profile and CA certificate paths must be absolute")
    payload = json.loads(profile_path.read_text(encoding="utf-8"))
    expected_fields = {
        "schema",
        "device_id",
        "relay_base_uri",
        "certificate_sha256",
        "client_id",
        "client_secret_base64",
        "scopes",
    }
    if not isinstance(payload, dict) or set(payload) != expected_fields:
        raise ValueError("relay security profile fields are invalid")
    if payload["schema"] != SECURITY_PROFILE_SCHEMA:
        raise ValueError("relay security profile schema is invalid")
    parsed = urllib.parse.urlparse(str(payload["relay_base_uri"]))
    profile_port = parsed.port or 443
    if (
        parsed.scheme != "https"
        or parsed.hostname != host
        or profile_port != port
        or parsed.path != "/"
        or parsed.params
        or parsed.query
        or parsed.fragment
    ):
        raise ValueError("relay host/port do not match the pinned security profile")
    device_id = str(payload["device_id"])
    client_id = str(payload["client_id"])
    if not (device_id.isdecimal() and len(device_id) == 6) or not client_id:
        raise ValueError("relay security identity is invalid")
    try:
        secret = base64.b64decode(str(payload["client_secret_base64"]).encode("ascii"), validate=True)
        pinned_fingerprint = bytes.fromhex(str(payload["certificate_sha256"]))
    except (ValueError, UnicodeError) as exc:
        raise ValueError("relay security profile encoding is invalid") from exc
    if len(secret) != 32 or len(pinned_fingerprint) != 32:
        raise ValueError("relay security profile key material is invalid")
    scopes = tuple(sorted(set(str(value) for value in payload["scopes"])))
    requested = tuple(sorted(set(required_scopes)))
    if not requested or not set(requested).issubset(scopes):
        raise ValueError("relay security profile lacks required scopes")
    certificate_der = ssl.PEM_cert_to_DER_cert(ca_path.read_text(encoding="ascii"))
    if not hmac.compare_digest(hashlib.sha256(certificate_der).digest(), pinned_fingerprint):
        raise ValueError("CA certificate does not match the pinned relay fingerprint")
    ssl_context = ssl.create_default_context(cafile=str(ca_path))
    if hasattr(ssl, "TLSVersion"):
        ssl_context.minimum_version = ssl.TLSVersion.TLSv1_2
    return {
        "base_url": str(payload["relay_base_uri"]),
        "host": host,
        "port": port,
        "device_id": device_id,
        "client_id": client_id,
        "secret": secret,
        "scopes": requested,
        "fingerprint": pinned_fingerprint,
        "ssl_context": ssl_context,
    }


def https_request(profile, path, authorization="", json_payload=None):
    headers = {"Accept": "application/json"}
    data = None
    if authorization:
        headers["Authorization"] = authorization
    if json_payload is not None:
        headers["Content-Type"] = "application/json"
        data = json.dumps(json_payload, separators=(",", ":")).encode("utf-8")
    request = urllib.request.Request(
        urllib.parse.urljoin(profile["base_url"], path.lstrip("/")),
        data=data,
        headers=headers,
        method="POST" if data is not None else "GET",
    )
    opener = urllib.request.build_opener(
        urllib.request.HTTPSHandler(context=profile["ssl_context"])
    )
    with opener.open(request, timeout=5) as response:
        return response.read()


def authenticate(profile):
    challenge = json.loads(https_request(
        profile,
        "/remote/auth/challenge?client_id="
        + urllib.parse.quote(profile["client_id"], safe=""),
    ))
    if (
        challenge.get("schema") != "v5.remote_auth_challenge.v1"
        or challenge.get("protocol") != AUTH_PROTOCOL
        or challenge.get("device_id") != profile["device_id"]
    ):
        raise RuntimeError("relay challenge identity is invalid")
    challenge_id = str(challenge.get("challenge_id") or "")
    nonce = str(challenge.get("nonce") or "")
    if not challenge_id or not nonce:
        raise RuntimeError("relay challenge is incomplete")
    canonical = (
        AUTH_PROTOCOL + "\n"
        + profile["client_id"] + "\n"
        + challenge_id + "\n"
        + nonce + "\n"
        + profile["device_id"] + "\n"
        + ",".join(profile["scopes"])
    ).encode("utf-8")
    mac = base64.urlsafe_b64encode(
        hmac.new(profile["secret"], canonical, hashlib.sha256).digest()
    ).decode("ascii").rstrip("=")
    session = json.loads(https_request(profile, "/remote/auth/session", json_payload={
        "schema": "v5.remote_auth_session_request.v1",
        "protocol": AUTH_PROTOCOL,
        "client_id": profile["client_id"],
        "challenge_id": challenge_id,
        "nonce": nonce,
        "requested_scopes": list(profile["scopes"]),
        "mac": mac,
    }))
    if (
        session.get("schema") != "v5.remote_auth_session.v1"
        or session.get("protocol") != AUTH_PROTOCOL
        or session.get("device_id") != profile["device_id"]
        or session.get("client_id") != profile["client_id"]
    ):
        raise RuntimeError("relay session identity is invalid")
    token = str(session.get("session_token") or "")
    if not token:
        raise RuntimeError("relay session token is missing")
    return SESSION_SCHEME + " " + token


def capture_frame(profile, authorization, out_bmp, out_json):
    payload = https_request(profile, "/remote/frame/full", authorization=authorization)
    if len(payload) < 4:
        raise RuntimeError("remote frame payload is truncated")
    meta_len = struct.unpack("<I", payload[:4])[0]
    if meta_len <= 0 or 4 + meta_len > len(payload):
        raise RuntimeError("remote frame metadata length is invalid")
    meta = json.loads(payload[4:4 + meta_len].decode("utf-8"))
    pixels = payload[4 + meta_len:]
    width = int(meta["width"])
    height = int(meta["height"])
    stride = int(meta["stride"])
    if meta.get("format") != "bgra32":
        raise SystemExit(f"unsupported frame format: {meta.get('format')}")
    row_stride = width * 4
    image_size = row_stride * height
    if stride < row_stride or len(pixels) < stride * height:
        raise RuntimeError("remote frame pixels are truncated")
    file_size = 14 + 40 + image_size
    with out_bmp.open("wb") as fp:
        fp.write(b"BM")
        fp.write(struct.pack("<IHHI", file_size, 0, 0, 54))
        fp.write(struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 32, 0, image_size, 2835, 2835, 0, 0))
        for y in range(height):
            start = y * stride
            fp.write(pixels[start:start + row_stride])
    out_json.write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return meta


def ws_send_text(sock, payload):
    data = payload.encode("utf-8")
    mask = os.urandom(4)
    if len(data) < 126:
        header = bytes([0x81, 0x80 | len(data)])
    elif len(data) <= 65535:
        header = bytes([0x81, 0x80 | 126]) + struct.pack("!H", len(data))
    else:
        raise ValueError("payload too large")
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    sock.sendall(header + mask + masked)


def ws_recv_text(sock):
    first = sock.recv(2)
    if len(first) != 2:
        raise RuntimeError("websocket closed")
    opcode = first[0] & 0x0F
    length = first[1] & 0x7F
    if opcode == 0x8:
        raise RuntimeError("websocket close frame")
    if opcode != 0x1:
        raise RuntimeError(f"unexpected websocket opcode {opcode}")
    if length == 126:
        length = struct.unpack("!H", sock.recv(2))[0]
    elif length == 127:
        raise RuntimeError("large websocket frames are not supported")
    payload = bytearray()
    while len(payload) < length:
        chunk = sock.recv(length - len(payload))
        if not chunk:
            raise RuntimeError("websocket closed during payload")
        payload.extend(chunk)
    return json.loads(payload.decode("utf-8"))


def ws_connect(profile, authorization):
    host = profile["host"]
    port = profile["port"]
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    request = (
        f"GET /remote/input HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        f"Authorization: {authorization}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n"
    ).encode("ascii")
    raw_sock = socket.create_connection((host, port), timeout=5)
    try:
        sock = profile["ssl_context"].wrap_socket(raw_sock, server_hostname=host)
    except Exception:
        raw_sock.close()
        raise
    peer_certificate = sock.getpeercert(binary_form=True)
    if not hmac.compare_digest(
        hashlib.sha256(peer_certificate).digest(),
        profile["fingerprint"],
    ):
        sock.close()
        raise RuntimeError("relay WebSocket certificate fingerprint mismatch")
    sock.sendall(request)
    response = b""
    while b"\r\n\r\n" not in response:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("websocket handshake closed")
        response += chunk
    header = response.decode("iso-8859-1", errors="replace")
    if "101 Switching Protocols" not in header:
        raise RuntimeError(f"websocket handshake failed: {header.splitlines()[0] if header else 'empty'}")
    accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")).digest()).decode("ascii")
    if accept not in header:
        raise RuntimeError("websocket accept key mismatch")
    return sock


def send_pointer(sock, session_id, seq, phase, x, y):
    ws_send_text(sock, json.dumps({
        "type": "pointer_event",
        "session_id": session_id,
        "source": "v5_remote_input_evidence",
        "seq": seq,
        "phase": phase,
        "x": int(x),
        "y": int(y),
        "button": "left",
        "client_time_ms": int(time.time() * 1000),
    }, separators=(",", ":")))
    ack = ws_recv_text(sock)
    if not ack.get("accepted"):
        raise RuntimeError(f"pointer {phase} rejected: {ack}")
    return ack


def remote_action(profile, authorization, kind, args):
    session_id = f"v5-evidence-{int(time.time() * 1000)}"
    with ws_connect(profile, authorization) as sock:
        ws_send_text(sock, json.dumps({
            "type": "control_request",
            "session_id": session_id,
            "source": "v5_remote_input_evidence",
            "client_time_ms": int(time.time() * 1000),
        }, separators=(",", ":")))
        grant = ws_recv_text(sock)
        if grant.get("type") != "control_grant" or not grant.get("accepted"):
            raise RuntimeError(f"remote input not granted: {grant}")
        acks = []
        seq = 1
        if kind == "click":
            acks.append(send_pointer(sock, session_id, seq, "down", args.x, args.y))
            seq += 1
            acks.append(send_pointer(sock, session_id, seq, "up", args.x, args.y))
        else:
            acks.append(send_pointer(sock, session_id, seq, "down", args.x1, args.y1))
            seq += 1
            steps = max(2, min(int(args.steps), 40))
            for i in range(1, steps + 1):
                x = args.x1 + ((args.x2 - args.x1) * i) // steps
                y = args.y1 + ((args.y2 - args.y1) * i) // steps
                acks.append(send_pointer(sock, session_id, seq, "move", x, y))
                seq += 1
            acks.append(send_pointer(sock, session_id, seq, "up", args.x2, args.y2))
        return {"ok": True, "protocol": "v3_pointer_ws", "session_id": session_id, "grant": grant, "acks": acks}

def main():
    build_root = Path(os.environ.get("V5_BUILD_ROOT", Path.home() / "v5-build"))
    evidence_root = Path(os.environ.get("V5_EVIDENCE_ROOT", build_root / "evidence"))
    parser = argparse.ArgumentParser(
        description="Capture before/after frames around one v3-compatible WebSocket pointer input action."
    )
    parser.add_argument("--ssh", help="optional board SSH host for event-delta collection")
    parser.add_argument("--host", default="192.168.1.221", help="relay host/IP")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--relay-security-profile", required=True)
    parser.add_argument("--ca-cert", required=True)
    parser.add_argument("--out-dir", default=str(evidence_root / "board_remote_input"))
    sub = parser.add_subparsers(dest="kind", required=True)
    click = sub.add_parser("click")
    click.add_argument("--x", type=int, required=True)
    click.add_argument("--y", type=int, required=True)
    drag = sub.add_parser("drag")
    drag.add_argument("--x1", type=int, required=True)
    drag.add_argument("--y1", type=int, required=True)
    drag.add_argument("--x2", type=int, required=True)
    drag.add_argument("--y2", type=int, required=True)
    drag.add_argument("--steps", type=int, default=12)
    sub.add_parser("capture")
    args = parser.parse_args()

    requested_scopes = {"viewer"} if args.kind == "capture" else {"viewer", "operator"}
    profile = load_security_profile(
        args.relay_security_profile,
        args.ca_cert,
        args.host,
        args.port,
        requested_scopes,
    )
    authorization = authenticate(profile)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    if args.kind == "capture":
        frame = out_dir / f"v5_board_capture_{stamp}.bmp"
        meta_path = out_dir / f"v5_board_capture_{stamp}.json"
        capture_frame(profile, authorization, frame, meta_path)
        print(frame)
        return
    before_bmp = out_dir / f"v5_remote_input_{stamp}_before.bmp"
    before_json = out_dir / f"v5_remote_input_{stamp}_before.json"
    after_bmp = out_dir / f"v5_remote_input_{stamp}_after.bmp"
    after_json = out_dir / f"v5_remote_input_{stamp}_after.json"
    ui_delta = out_dir / f"v5_remote_input_{stamp}_ui_delta.jsonl"
    input_delta = out_dir / f"v5_remote_input_{stamp}_remote_input_delta.jsonl"
    summary = out_dir / f"v5_remote_input_{stamp}_summary.json"

    before_meta = capture_frame(profile, authorization, before_bmp, before_json)
    before_ui = count_remote_lines(args.ssh, "/run/8ax_v5_product_ui/ui_events.jsonl") if args.ssh else None
    before_input = count_remote_lines(args.ssh, "/run/8ax_v5_product_ui/remote_input_events.jsonl") if args.ssh else None

    result = remote_action(profile, authorization, args.kind, args)
    time.sleep(0.2)
    after_meta = capture_frame(profile, authorization, after_bmp, after_json)
    after_ui = count_remote_lines(args.ssh, "/run/8ax_v5_product_ui/ui_events.jsonl") if args.ssh else None
    after_input = count_remote_lines(args.ssh, "/run/8ax_v5_product_ui/remote_input_events.jsonl") if args.ssh else None
    if args.ssh:
        fetch_delta(args.ssh, "/run/8ax_v5_product_ui/ui_events.jsonl", before_ui, ui_delta)
        fetch_delta(args.ssh, "/run/8ax_v5_product_ui/remote_input_events.jsonl", before_input, input_delta)

    summary.write_text(json.dumps({
        "schema": "v5.remote_input_evidence.v1",
        "kind": args.kind,
        "mode": "layout_only",
        "transport": "https_wss_challenge_session",
        "device_id": profile["device_id"],
        "client_id": profile["client_id"],
        "scopes": list(profile["scopes"]),
        "before_frame": str(before_bmp),
        "after_frame": str(after_bmp),
        "before_meta": before_meta,
        "after_meta": after_meta,
        "before_ui_events": before_ui,
        "after_ui_events": after_ui,
        "before_remote_input_events": before_input,
        "after_remote_input_events": after_input,
        "result": result,
        "note": "frames captured before/after remote input; without --ssh, pair with board_exec event/native readback; this is not real-finger hardware or motion evidence by itself",
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(summary)


if __name__ == "__main__":
    main()
