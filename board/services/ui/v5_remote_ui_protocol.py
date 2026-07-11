from __future__ import annotations

import base64
import hashlib
import os
import socket
import struct
import time

from v5_remote_ui_contract import FramePayload, PIXEL_FORMAT, PayloadViews, WS_GUID

def ws_accept_value(key: str) -> str:
    digest = hashlib.sha1((key + WS_GUID).encode("ascii")).digest()
    return base64.b64encode(digest).decode("ascii")


def payload_parts(payload: FramePayload) -> list[memoryview]:
    if isinstance(payload, PayloadViews):
        return payload.parts
    return [memoryview(payload)]


def send_payload(sock: socket.socket, payload: FramePayload) -> None:
    if not payload:
        return
    parts = payload_parts(payload)
    if len(parts) == 1:
        sock.sendall(parts[0])
        return
    if not hasattr(sock, "sendmsg"):
        for part in parts:
            if part:
                sock.sendall(part)
        return
    views = [part for part in parts if part]
    max_iov = 256
    if "SC_IOV_MAX" in os.sysconf_names:
        max_iov = min(int(os.sysconf("SC_IOV_MAX")), max_iov)
    while views:
        chunk = views[:max_iov]
        sent = sock.sendmsg(chunk)
        if sent <= 0:
            raise ConnectionError("socket sendmsg returned no progress")
        remaining = sent
        while views and remaining >= len(views[0]):
            remaining -= len(views[0])
            views.pop(0)
        if views and remaining > 0:
            views[0] = views[0][remaining:]


def send_ws_frame(sock: socket.socket, opcode: int, payload: FramePayload) -> None:
    first = bytes([0x80 | opcode])
    size = len(payload)
    if size < 126:
        header = first + bytes([size])
    elif size <= 65535:
        header = first + bytes([126]) + struct.pack("!H", size)
    else:
        header = first + bytes([127]) + struct.pack("!Q", size)
    sock.sendall(header)
    send_payload(sock, payload)


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("websocket closed")
        data.extend(chunk)
    return bytes(data)


def recv_ws_frame(sock: socket.socket) -> tuple[int, bytes]:
    header = recv_exact(sock, 2)
    opcode = header[0] & 0x0F
    masked = (header[1] & 0x80) != 0
    size = header[1] & 0x7F
    if size == 126:
        size = struct.unpack("!H", recv_exact(sock, 2))[0]
    elif size == 127:
        size = struct.unpack("!Q", recv_exact(sock, 8))[0]
    if not masked:
        raise ConnectionError("client websocket frame is not masked")
    mask = recv_exact(sock, 4)
    payload = bytearray(recv_exact(sock, size))
    for index, value in enumerate(payload):
        payload[index] = value ^ mask[index & 3]
    return opcode, bytes(payload)


def frame_metadata(kind: str, frame_id: int, base_frame_id: int, width: int, height: int, stride: int, rects: list[dict]) -> dict:
    return {
        "type": kind,
        "frame_id": frame_id,
        "base_frame_id": base_frame_id,
        "monotonic_ns": time.monotonic_ns(),
        "width": width,
        "height": height,
        "stride": stride,
        "format": PIXEL_FORMAT,
        "dirty_count": len(rects),
        "rects": rects,
    }
