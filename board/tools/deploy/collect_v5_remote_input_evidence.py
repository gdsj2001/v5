#!/usr/bin/env python3
import argparse
import base64
import hashlib
import json
import os
import socket
import struct
import subprocess
import time
import urllib.request
from pathlib import Path


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
    ssh(host, f"test -f {remote!r} || :")
    cmd = f"test -f {remote!r} && tail -n +{before + 1} {remote!r} > /tmp/v5_remote_input_delta.tmp || :"
    ssh(host, cmd)
    scp_from(host, "/tmp/v5_remote_input_delta.tmp", local)


def capture_frame(host, port, out_bmp, out_json):
    url = f"http://{host}:{port}/remote/frame/full"
    with urllib.request.urlopen(url, timeout=5) as response:
        payload = response.read()
    meta_len = struct.unpack("<I", payload[:4])[0]
    meta = json.loads(payload[4:4 + meta_len].decode("utf-8"))
    pixels = payload[4 + meta_len:]
    width = int(meta["width"])
    height = int(meta["height"])
    stride = int(meta["stride"])
    if meta.get("format") != "bgra32":
        raise SystemExit(f"unsupported frame format: {meta.get('format')}")
    row_stride = width * 4
    image_size = row_stride * height
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


def ws_connect(host, port):
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    request = (
        f"GET /remote/input HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n"
    ).encode("ascii")
    sock = socket.create_connection((host, port), timeout=5)
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


def remote_action(host, port, kind, args):
    session_id = f"v5-evidence-{int(time.time() * 1000)}"
    with ws_connect(host, port) as sock:
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
    parser = argparse.ArgumentParser(
        description="Capture before/after frames around one v3-compatible WebSocket pointer input action."
    )
    parser.add_argument("--ssh", required=True, help="board SSH host")
    parser.add_argument("--host", default="192.168.1.221", help="relay host/IP")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--out-dir", default="/root/Desktop/v5/artifacts/board_remote_input")
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
    args = parser.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    before_bmp = out_dir / f"v5_remote_input_{stamp}_before.bmp"
    before_json = out_dir / f"v5_remote_input_{stamp}_before.json"
    after_bmp = out_dir / f"v5_remote_input_{stamp}_after.bmp"
    after_json = out_dir / f"v5_remote_input_{stamp}_after.json"
    ui_delta = out_dir / f"v5_remote_input_{stamp}_ui_delta.jsonl"
    input_delta = out_dir / f"v5_remote_input_{stamp}_remote_input_delta.jsonl"
    summary = out_dir / f"v5_remote_input_{stamp}_summary.json"

    before_meta = capture_frame(args.host, args.port, before_bmp, before_json)
    before_ui = count_remote_lines(args.ssh, "/run/8ax_v5_product_ui/ui_events.jsonl")
    before_input = count_remote_lines(args.ssh, "/run/8ax_v5_product_ui/remote_input_events.jsonl")

    result = remote_action(args.host, args.port, args.kind, args)
    time.sleep(0.2)
    after_meta = capture_frame(args.host, args.port, after_bmp, after_json)
    after_ui = count_remote_lines(args.ssh, "/run/8ax_v5_product_ui/ui_events.jsonl")
    after_input = count_remote_lines(args.ssh, "/run/8ax_v5_product_ui/remote_input_events.jsonl")
    fetch_delta(args.ssh, "/run/8ax_v5_product_ui/ui_events.jsonl", before_ui, ui_delta)
    fetch_delta(args.ssh, "/run/8ax_v5_product_ui/remote_input_events.jsonl", before_input, input_delta)

    summary.write_text(json.dumps({
        "schema": "v5.remote_input_evidence.v1",
        "kind": args.kind,
        "mode": "layout_only",
        "before_frame": str(before_bmp),
        "after_frame": str(after_bmp),
        "before_meta": before_meta,
        "after_meta": after_meta,
        "before_ui_events": before_ui,
        "after_ui_events": after_ui,
        "before_remote_input_events": before_input,
        "after_remote_input_events": after_input,
        "result": result,
        "note": "frame captured before remote input; this is not touch calibration, real-finger hardware, MDI/start, or motion evidence",
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(summary)


if __name__ == "__main__":
    main()
