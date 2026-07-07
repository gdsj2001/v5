#!/usr/bin/env python3
import argparse
import json
import struct
import subprocess
import time
import urllib.request
from pathlib import Path


def ssh(host, command):
    return subprocess.run(["ssh", host, command], check=True, text=True, capture_output=True).stdout


def scp_from(host, remote, local):
    subprocess.run(["scp", f"{host}:{remote}", str(local)], check=True)


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


def remote_action(host, port, kind, args):
    if kind == "click":
        path = f"/remote/input/layout-click?x={args.x}&y={args.y}&mode=layout_only"
    else:
        path = (
            f"/remote/input/layout-drag?x1={args.x1}&y1={args.y1}"
            f"&x2={args.x2}&y2={args.y2}&steps={args.steps}&mode=layout_only"
        )
    url = f"http://{host}:{port}{path}"
    with urllib.request.urlopen(url, timeout=5) as response:
        body = response.read().decode("utf-8")
    return json.loads(body)


def main():
    parser = argparse.ArgumentParser(
        description="Capture before/after frames around one restricted v5 layout-only remote input action."
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
