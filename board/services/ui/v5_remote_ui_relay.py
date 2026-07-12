#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import socket
import socketserver
import struct
import sys
import urllib.parse
from http.server import BaseHTTPRequestHandler
from pathlib import Path

from v5_remote_ui_contract import (
    DIRTY_EVENT_HISTORY_LIMIT,
    DIRTY_FIFO_EMPTY_SLEEP_SECONDS,
    DIRTY_FIFO_NAME,
    FRAMEBUFFER_NAME,
    INPUT_FIFO_NAME,
    LARGE_DIRTY_MIN_INTERVAL_SECONDS,
    LARGE_DIRTY_PIXEL_RATIO,
    MAX_DIRTY_RECTS_PER_FRAME,
    PIXEL_FORMAT,
    PROTOCOL_VERSION,
    RUN_DIR,
    STREAM_COALESCE_SECONDS,
    STREAM_IDLE_PING_SECONDS,
    STREAM_TARGET_FPS,
    FramePayload,
    PayloadViews,
)
from v5_remote_ui_protocol import frame_metadata, recv_ws_frame, send_ws_frame, ws_accept_value
from v5_remote_ui_state import DirtyReader, FrameState
from v5_remote_ui_support import (
    CpuUsageSampler,
    cpu_samples_snapshot,
    now_ms,
    parse_allow_cidrs,
    peer_allowed,
    process_diagnostics,
    system_metrics,
)

class RemoteRelayHandler(BaseHTTPRequestHandler):
    server_version = "V5RemoteUiRelay/1"

    def log_message(self, fmt: str, *args) -> None:
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    @property
    def state(self) -> FrameState:
        return self.server.state

    def check_peer(self) -> bool:
        peer = self.client_address[0]
        if peer_allowed(peer, self.server.allow_networks):
            return True
        self.write_json(403, {"ok": False, "error": "remote_peer_not_allowed"})
        return False

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path.startswith("/remote/") and not self.check_peer():
            return
        if parsed.path.startswith("/remote/") and parsed.path != "/remote/diagnostics" and not self.state.ui_ready():
            self.write_json(503, {
                "ok": False,
                "error": "ui_not_ready",
                "ready_path": str(self.state.ready_path),
            })
            return
        if parsed.path == "/remote/info":
            self.handle_info()
        elif parsed.path == "/remote/frame/full":
            self.handle_full_frame()
        elif parsed.path == "/remote/stream" and self.is_ws_request():
            self.handle_stream()
        elif parsed.path == "/remote/input" and self.is_ws_request():
            self.handle_input()
        elif parsed.path == "/remote/diagnostics":
            ready_metadata = self.state.ready_metadata()
            self.write_json(200, {
                "schema": "re.v5.remote_diagnostics.v1",
                "protocol_version": PROTOCOL_VERSION,
                "ui_ready": ready_metadata is not None,
                "ready_metadata": ready_metadata,
                "framebuffer": str(self.state.framebuffer_path),
                "dirty_fifo": str(self.state.dirty_fifo_path),
                "input_fifo": str(self.state.input_fifo_path),
                "frame_id": self.state.frame_id,
                "first_dirty_event": self.state.first_dirty_event(),
                "recent_dirty_events": self.state.recent_dirty_events(),
                "metrics": self.state.metrics_snapshot(),
                "cpu_samples": cpu_samples_snapshot(),
                "process": process_diagnostics(),
            })
        else:
            self.write_json(404, {"ok": False, "error": "unsupported_remote_path"})

    def handle_info(self) -> None:
        input_enabled = os.environ.get("V5_UI_REMOTE_INPUT", "off") == "layout_only"
        self.write_json(200, {
            "protocol_version": PROTOCOL_VERSION,
            "ui_ready": True,
            "ready_metadata": self.state.ready_metadata(),
            "width": self.state.width,
            "height": self.state.height,
            "pixel_format": PIXEL_FORMAT,
            "stride": self.state.stride,
            "view_only": not input_enabled,
            "input_enabled": input_enabled,
            "system_metrics": system_metrics(),
        })

    def handle_full_frame(self) -> None:
        frame = self.state.full_frame()
        if frame is None:
            self.write_json(503, {"ok": False, "error": "remote_framebuffer_unavailable"})
            return
        self.state.mark_metric("full_frame_requests")
        frame_id, payload = frame
        meta = frame_metadata("full_frame", frame_id, 0, self.state.width, self.state.height, self.state.stride, [])
        meta_bytes = json.dumps(meta, separators=(",", ":")).encode("utf-8")
        self.write_frame_envelope(meta_bytes, payload)

    def handle_stream(self) -> None:
        if not self.accept_websocket():
            return
        self.state.mark_metric("stream_sessions")
        self.state.mark_metric("stream_active_sessions")
        sock = self.connection
        last_large_dirty_sent_at = 0.0
        try:
            frame = self.state.full_frame()
            if frame is None:
                return
            frame_id, payload = frame
            self.state.mark_metric("stream_initial_full_frames")
            self.send_frame(sock, frame_metadata("full_frame", frame_id, 0, self.state.width, self.state.height, self.state.stride, []), payload)
            last_sent = frame_id
            while True:
                batch = self.state.wait_dirty_batch_after(last_sent, STREAM_IDLE_PING_SECONDS, STREAM_COALESCE_SECONDS)
                if batch is None:
                    send_ws_frame(sock, 0x9, b"")
                    self.state.mark_metric("stream_idle_pings")
                    continue
                if batch.get("needs_full"):
                    frame = self.state.full_frame()
                    if frame is None:
                        continue
                    frame_id, payload = frame
                    self.state.mark_metric("stream_repair_missing_dirty_events")
                    self.state.mark_metric("stream_repair_full_frames")
                    self.send_frame(sock, frame_metadata("full_frame", frame_id, 0, self.state.width, self.state.height, self.state.stride, []), payload)
                    last_sent = frame_id
                    continue
                last_large_dirty_sent_at = self.state.throttle_large_dirty(batch, last_large_dirty_sent_at)
                prepared = self.state.dirty_payload(batch)
                if prepared is None:
                    continue
                payload, rects = prepared
                merged = int(batch.get("merged_events", 1))
                if merged > 1:
                    self.state.mark_metric("dirty_coalesced_events", merged - 1)
                meta = frame_metadata("dirty_rects", int(batch["frame_id"]), int(batch["base_frame_id"]), self.state.width, self.state.height, self.state.stride, rects)
                self.state.mark_metric("dirty_rect_frames")
                self.send_frame(sock, meta, payload)
                last_sent = int(batch["frame_id"])
        except (BrokenPipeError, ConnectionError, ConnectionResetError, OSError):
            self.state.mark_metric("stream_send_failures")
        finally:
            self.state.mark_metric("stream_disconnects")
            self.state.decrement_metric_floor("stream_active_sessions")

    def handle_input(self) -> None:
        if not self.accept_websocket():
            return
        self.state.mark_metric("input_sessions")
        self.state.mark_metric("input_active_sessions")
        input_enabled = os.environ.get("V5_UI_REMOTE_INPUT", "off") == "layout_only"
        session_id = ""
        sock = self.connection
        try:
            while True:
                opcode, payload = recv_ws_frame(sock)
                if opcode == 0x8:
                    return
                if opcode != 0x1:
                    return
                self.state.mark_metric("input_messages")
                try:
                    message = json.loads(payload.decode("utf-8"))
                except json.JSONDecodeError:
                    self.send_ack(sock, "pointer_reject", session_id, 0, "unknown", False, "invalid_json")
                    continue
                msg_type = str(message.get("type") or "")
                if msg_type == "control_request":
                    session_id = str(message.get("session_id") or "")
                    self.send_ack(sock, "control_grant" if input_enabled and session_id else "control_revoke", session_id, 0, "control", input_enabled and bool(session_id), None if input_enabled and session_id else "remote_input_disabled")
                elif msg_type == "pointer_event":
                    seq = int(message.get("seq") or 0)
                    phase = str(message.get("phase") or "unknown")
                    message_session = str(message.get("session_id") or "")
                    if not input_enabled:
                        self.send_ack(sock, "pointer_reject", message_session, seq, phase, False, "remote_input_disabled")
                    elif session_id and message_session != session_id:
                        self.send_ack(sock, "pointer_reject", message_session, seq, phase, False, "session_mismatch")
                    elif self.write_input_event(message):
                        self.send_ack(sock, "pointer_ack", message_session, seq, phase, True, None)
                    else:
                        self.send_ack(sock, "pointer_reject", message_session, seq, phase, False, "remote_input_ipc_unavailable")
                else:
                    self.send_ack(sock, "pointer_reject", session_id, 0, "unknown", False, "unsupported_type")
        except (BrokenPipeError, ConnectionError, ConnectionResetError, OSError):
            pass
        finally:
            self.state.mark_metric("input_disconnects")
            self.state.decrement_metric_floor("input_active_sessions")

    def write_input_event(self, message: dict) -> bool:
        try:
            self.state.run_dir.mkdir(parents=True, exist_ok=True)
            try:
                os.mkfifo(self.state.input_fifo_path, 0o600)
            except FileExistsError:
                pass
            fd = os.open(self.state.input_fifo_path, os.O_WRONLY | os.O_NONBLOCK)
            try:
                payload = json.dumps(message, separators=(",", ":")).encode("utf-8") + b"\n"
                os.write(fd, payload)
                return True
            finally:
                os.close(fd)
        except OSError:
            return False

    def send_ack(self, sock: socket.socket, msg_type: str, session_id: str, seq: int, phase: str, accepted: bool, reason: str | None) -> None:
        if msg_type == "pointer_ack":
            self.state.mark_metric("input_accepted")
        elif msg_type == "pointer_reject":
            self.state.mark_metric("input_rejected")
        payload = {
            "type": msg_type,
            "session_id": session_id,
            "seq": seq,
            "phase": phase,
            "accepted": accepted,
            "server_time_ms": now_ms(),
            "reason": reason,
        }
        send_ws_frame(sock, 0x1, json.dumps(payload, separators=(",", ":")).encode("utf-8"))

    def send_frame(self, sock: socket.socket, meta: dict, payload: FramePayload) -> None:
        send_ws_frame(sock, 0x1, json.dumps(meta, separators=(",", ":")).encode("utf-8"))
        send_ws_frame(sock, 0x2, payload)

    def is_ws_request(self) -> bool:
        return self.headers.get("Upgrade", "").lower() == "websocket"

    def accept_websocket(self) -> bool:
        key = self.headers.get("Sec-WebSocket-Key", "")
        if not key:
            self.write_json(400, {"ok": False, "error": "missing_websocket_key"})
            return False
        response = (
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Accept: {ws_accept_value(key)}\r\n\r\n"
        ).encode("ascii")
        self.connection.sendall(response)
        self.close_connection = True
        return True

    def write_json(self, status: int, body: dict) -> None:
        payload = json.dumps(body, separators=(",", ":")).encode("utf-8") + b"\n"
        self.write_bytes(status, "application/json; charset=utf-8", payload)

    def write_bytes(self, status: int, content_type: str, payload: bytes) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(payload)

    def write_frame_envelope(self, meta_bytes: bytes, payload: FramePayload) -> None:
        meta_len = struct.pack("<I", len(meta_bytes))
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(meta_len) + len(meta_bytes) + len(payload)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(meta_len)
        self.wfile.write(meta_bytes)
        self.wfile.write(payload)


class RemoteRelayServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, address, handler, state: FrameState, allow_networks):
        super().__init__(address, handler)
        self.state = state
        self.allow_networks = allow_networks


def main() -> int:
    parser = argparse.ArgumentParser(description="v5 remote UI relay using UI mmap/FIFO IPC.")
    parser.add_argument("--host", default=os.environ.get("V5_UI_REMOTE_BIND", "0.0.0.0"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("V5_UI_REMOTE_PORT", "18080")))
    parser.add_argument("--allow-cidrs", default=os.environ.get("V5_UI_REMOTE_ALLOW_CIDRS", "127.0.0.1/32,192.168.1.0/24"))
    parser.add_argument("--run-dir", default=str(RUN_DIR))
    parser.add_argument("--width", type=int, default=int(os.environ.get("V5_UI_REMOTE_WIDTH", "1024")))
    parser.add_argument("--height", type=int, default=int(os.environ.get("V5_UI_REMOTE_HEIGHT", "600")))
    parser.add_argument("--ready-path", default=os.environ.get("V5_UI_READY_PATH", str(RUN_DIR / "ui_ready.json")))
    args = parser.parse_args()

    state = FrameState(Path(args.run_dir), args.width, args.height, Path(args.ready_path))
    dirty_reader = DirtyReader(state)
    dirty_reader.start()
    allow_networks = parse_allow_cidrs(args.allow_cidrs)
    with RemoteRelayServer((args.host, args.port), RemoteRelayHandler, state, allow_networks) as server:
        print(f"v5_remote_ui_relay listening host={args.host} port={args.port} run_dir={state.run_dir} ready_path={state.ready_path} allow_cidrs={args.allow_cidrs}", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            return 0
        finally:
            dirty_reader.stop_requested.set()
            state.close_framebuffer_map()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
