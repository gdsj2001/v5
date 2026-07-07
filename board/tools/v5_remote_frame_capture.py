#!/usr/bin/env python3
"""Capture a board remote relay full-frame packet as a BMP image."""

from __future__ import annotations

import argparse
import json
import struct
import urllib.request
from pathlib import Path
from typing import Any


def decode_full_frame_packet(packet: bytes) -> tuple[dict[str, Any], bytes]:
    if len(packet) < 4:
        raise ValueError("remote frame packet is too short for metadata length")
    meta_len = struct.unpack("<I", packet[:4])[0]
    meta_start = 4
    meta_end = meta_start + meta_len
    if meta_len <= 0 or meta_end > len(packet):
        raise ValueError("remote frame packet metadata length is invalid")
    metadata = json.loads(packet[meta_start:meta_end].decode("utf-8"))
    payload = packet[meta_end:]
    width = int(metadata.get("width", 0) or 0)
    height = int(metadata.get("height", 0) or 0)
    stride = int(metadata.get("stride", width * 4) or 0)
    pixel_format = str(metadata.get("format") or metadata.get("pixel_format") or "")
    if width <= 0 or height <= 0 or stride < width * 4:
        raise ValueError(f"remote frame metadata has invalid dimensions: {metadata!r}")
    if pixel_format.lower() != "bgra32":
        raise ValueError(f"unsupported remote frame format: {pixel_format!r}")
    expected = stride * height
    if len(payload) != expected:
        raise ValueError(f"remote frame payload size mismatch: got {len(payload)}, expected {expected}")
    return metadata, payload


def validate_expected_size(metadata: dict[str, Any], expected_width: int, expected_height: int) -> None:
    width = int(metadata["width"])
    height = int(metadata["height"])
    if width != expected_width or height != expected_height:
        raise ValueError(
            f"remote frame size mismatch: got {width}x{height}, "
            f"expected {expected_width}x{expected_height}"
        )


def bgra_to_bmp(metadata: dict[str, Any], payload: bytes) -> bytes:
    width = int(metadata["width"])
    height = int(metadata["height"])
    stride = int(metadata["stride"])
    pixels = b"".join(payload[y * stride : y * stride + width * 4] for y in range(height - 1, -1, -1))
    file_size = 54 + len(pixels)
    return (
        b"BM"
        + struct.pack("<IHHI", file_size, 0, 0, 54)
        + struct.pack("<IIIHHIIIIII", 40, width, height, 1, 32, 0, len(pixels), 2835, 2835, 0, 0)
        + pixels
    )


def capture_packet(host: str, port: int, timeout_s: float) -> bytes:
    url = f"http://{host}:{int(port)}/remote/frame/full"
    with urllib.request.urlopen(url, timeout=float(timeout_s)) as response:
        return response.read()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.1.221")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--timeout-s", type=float, default=20.0)
    parser.add_argument("--out", required=True, help="Output BMP path.")
    parser.add_argument("--meta-out", default="", help="Optional JSON metadata output path.")
    parser.add_argument("--expected-width", type=int, default=1024)
    parser.add_argument("--expected-height", type=int, default=600)
    parser.add_argument("--allow-any-size", action="store_true", help="Diagnostic only: do not enforce 1024x600.")
    args = parser.parse_args()

    packet = capture_packet(args.host, args.port, args.timeout_s)
    metadata, payload = decode_full_frame_packet(packet)
    if not args.allow_any_size:
        validate_expected_size(metadata, args.expected_width, args.expected_height)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(bgra_to_bmp(metadata, payload))
    if args.meta_out:
        meta_out = Path(args.meta_out)
        meta_out.parent.mkdir(parents=True, exist_ok=True)
        meta_out.write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"ok": True, "out": str(out), "metadata": metadata}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
