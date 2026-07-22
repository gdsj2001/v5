#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ipaddress
import json
import os
import socket
import socketserver
import struct
import sys
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler
from pathlib import Path

from v5_remote_ui_auth import AuthError, AuthStore, build_server_tls_context
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
from v5_remote_ui_programs import MAX_GCODE_BYTES, ProgramApiError, ProgramFileService
from v5_remote_ui_native_packer import NativeDirtyPacker
from v5_remote_ui_shared_payload import (
    PreparedDirtyFrame,
    PreparedFullFrame,
    SharedDirtyPayloadProducer,
)
from v5_remote_ui_state import DirtyReader, FrameState
from v5_remote_ui_support import (
    cpu_samples_snapshot,
    now_ms,
    parse_allow_cidrs,
    peer_allowed,
    process_diagnostics,
    system_metrics,
)


def input_ws_frame_action(opcode: int) -> str:
    if opcode == 0x1:
        return "message"
    if opcode == 0x9:
        return "pong"
    if opcode == 0xA:
        return "continue"
    return "close"


def post_repair_delivery_action(
    waiting_for_post_repair_delta: bool,
    repair_target_frame_id: int,
    has_prepared_delta: bool,
) -> str:
    if repair_target_frame_id > 0:
        return "disconnect" if waiting_for_post_repair_delta else "repair"
    return "delta" if has_prepared_delta else "continue"


def stream_ws_receive_loop(
    sock: socket.socket,
    stop_event: threading.Event,
    send_lock: threading.Lock,
    wake_waiters,
    mark_metric,
) -> None:
    try:
        while not stop_event.is_set():
            opcode, payload = recv_ws_frame(sock)
            action = input_ws_frame_action(opcode)
            if action == "close":
                if opcode == 0x8:
                    with send_lock:
                        send_ws_frame(sock, 0x8, payload)
                    mark_metric("stream_client_closes")
                return
            if action == "pong":
                with send_lock:
                    send_ws_frame(sock, 0xA, payload)
                mark_metric("stream_client_pings")
            elif action == "continue":
                mark_metric("stream_client_pongs")
    except (BrokenPipeError, ConnectionError, ConnectionResetError, OSError):
        mark_metric("stream_receive_disconnects")
    finally:
        stop_event.set()
        wake_waiters()


def remote_path_requires_ui(path: str) -> bool:
    return path != "/remote/diagnostics" and not path.startswith("/remote/program/")


def required_remote_scope(method: str, path: str):
    if method == "GET" and path in ("/remote/info", "/remote/frame/full", "/remote/stream"):
        return "viewer"
    if method == "GET" and path == "/remote/diagnostics":
        return "diagnostics"
    if path.startswith("/remote/program/"):
        return "program_manager"
    if method == "GET" and path == "/remote/input":
        return "operator"
    if method == "POST" and path.startswith("/remote/ota/"):
        return "ota_admin"
    return None


def url_contains_auth_token(parsed: urllib.parse.ParseResult) -> bool:
    query = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)
    forbidden = {"token", "session_token", "authorization", "auth"}
    return any(key.lower() in forbidden for key in query)

class RemoteRelayHandler(BaseHTTPRequestHandler):
    server_version = "V5RemoteUiRelay/1"

    def log_message(self, fmt: str, *args) -> None:
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    @property
    def state(self) -> FrameState:
        return self.server.state

    @property
    def program_service(self) -> ProgramFileService:
        return self.server.program_service

    def check_peer(self) -> bool:
        peer = self.client_address[0]
        if peer_allowed(peer, self.server.allow_networks):
            return True
        self.write_json(403, {"ok": False, "error": "remote_peer_not_allowed"})
        return False

    def check_loopback(self) -> bool:
        try:
            allowed = ipaddress.ip_address(self.client_address[0]).is_loopback
        except ValueError:
            allowed = False
        if not allowed:
            self.write_json(403, {"ok": False, "error": "local_health_loopback_only"})
        return allowed

    def authorize_remote(self, method: str, parsed: urllib.parse.ParseResult) -> bool:
        if url_contains_auth_token(parsed):
            self.write_json(400, {"ok": False, "error": "remote_auth_token_in_url_forbidden"})
            return False
        scope = required_remote_scope(method, parsed.path)
        if scope is None:
            return True
        try:
            self._auth_session = self.server.auth_store.authorize(
                self.headers.get("Authorization"),
                self.client_address[0],
                scope,
            )
        except AuthError as exc:
            self.write_auth_error(exc)
            return False
        return True

    def write_auth_error(self, error: AuthError) -> None:
        self.write_json(error.status, {
            "ok": False,
            "error": error.code,
            "message": error.message,
        })

    def handle_local_health(self) -> None:
        self.write_json(200, {
            "schema": "v5.remote_relay_local_health.v1",
            "ok": True,
            "dirty_reader_ready": self.state.dirty_fifo_path.exists(),
            "tls": True,
            "device_id": self.server.auth_store.device_id,
        })

    def handle_auth_challenge(self, parsed: urllib.parse.ParseResult) -> None:
        query = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)
        try:
            payload = self.server.auth_store.issue_challenge(
                self.query_value(query, "client_id"),
                self.client_address[0],
            )
            self.write_json(200, payload)
        except AuthError as exc:
            self.write_auth_error(exc)

    def handle_auth_session(self) -> None:
        try:
            content_length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            content_length = 0
        if content_length <= 0 or content_length > 8192:
            self.write_json(400, {"ok": False, "error": "remote_auth_request_invalid"})
            return
        try:
            payload = json.loads(self.rfile.read(content_length).decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            self.write_json(400, {"ok": False, "error": "remote_auth_request_invalid"})
            return
        required_fields = {
            "schema", "protocol", "client_id", "challenge_id", "nonce",
            "requested_scopes", "mac",
        }
        if not isinstance(payload, dict) or set(payload) != required_fields:
            self.write_json(400, {"ok": False, "error": "remote_auth_request_invalid"})
            return
        if (
            payload.get("schema") != "v5.remote_auth_session_request.v1" or
            payload.get("protocol") != "v5.remote.auth.v1" or
            not isinstance(payload.get("requested_scopes"), list)
        ):
            self.write_json(400, {"ok": False, "error": "remote_auth_request_invalid"})
            return
        try:
            result = self.server.auth_store.create_session(
                str(payload.get("client_id") or ""),
                self.client_address[0],
                str(payload.get("challenge_id") or ""),
                str(payload.get("nonce") or ""),
                payload["requested_scopes"],
                str(payload.get("mac") or ""),
            )
            self.write_json(200, result)
        except AuthError as exc:
            self.write_auth_error(exc)

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        self._auth_session = None
        if parsed.path == "/local/health":
            if self.check_loopback():
                self.handle_local_health()
            return
        if parsed.path.startswith("/remote/"):
            if not self.check_peer():
                return
            if parsed.path == "/remote/auth/challenge":
                if url_contains_auth_token(parsed):
                    self.write_json(400, {"ok": False, "error": "remote_auth_token_in_url_forbidden"})
                    return
                self.handle_auth_challenge(parsed)
                return
            if not self.authorize_remote("GET", parsed):
                return
        self._request_ready_metadata = None
        if parsed.path.startswith("/remote/") and remote_path_requires_ui(parsed.path):
            startup_status, lifecycle_metadata = self.state.lifecycle_snapshot()
            if startup_status != "ready":
                body = {
                    "ok": False,
                    "error": "ui_startup_failed" if startup_status == "failed" else "ui_not_ready",
                    "startup_status": startup_status,
                    "ready_path": str(self.state.ready_path),
                }
                if startup_status == "failed":
                    body["failure_metadata"] = lifecycle_metadata
                self.write_json(503, body)
                return
            self._request_ready_metadata = lifecycle_metadata
        if parsed.path == "/remote/info":
            self.handle_info()
        elif parsed.path == "/remote/frame/full":
            self.handle_full_frame()
        elif parsed.path == "/remote/stream" and self.is_ws_request():
            self.handle_stream()
        elif parsed.path == "/remote/input" and self.is_ws_request():
            self.handle_input()
        elif parsed.path == "/remote/diagnostics":
            startup_status, lifecycle_metadata = self.state.lifecycle_snapshot()
            self.write_json(200, {
                "schema": "re.v5.remote_diagnostics.v1",
                "protocol_version": PROTOCOL_VERSION,
                "startup_status": startup_status,
                "ui_ready": startup_status == "ready",
                "ready_metadata": lifecycle_metadata if startup_status == "ready" else None,
                "failure_metadata": lifecycle_metadata if startup_status == "failed" else None,
                "framebuffer": str(self.state.framebuffer_path),
                "dirty_fifo": str(self.state.dirty_fifo_path),
                "input_fifo": str(self.state.input_fifo_path),
                "frame_id": self.state.frame_id,
                "first_dirty_event": self.state.first_dirty_event(),
                "recent_dirty_events": self.state.recent_dirty_events(),
                "metrics": self.state.metrics_snapshot(),
                "cpu_samples": cpu_samples_snapshot(),
                "process": process_diagnostics(),
                "auth": self.server.auth_store.diagnostic_snapshot(),
            })
        elif parsed.path == "/remote/program/list":
            self.handle_program_list()
        elif parsed.path == "/remote/program/file":
            self.handle_program_file_get(parsed)
        else:
            self.write_json(404, {"ok": False, "error": "unsupported_remote_path"})

    def do_POST(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        self._auth_session = None
        if parsed.path.startswith("/remote/"):
            if not self.check_peer():
                return
            if parsed.path == "/remote/auth/session":
                if url_contains_auth_token(parsed):
                    self.write_json(400, {"ok": False, "error": "remote_auth_token_in_url_forbidden"})
                    return
                self.handle_auth_session()
                return
            if not self.authorize_remote("POST", parsed):
                return
        if parsed.path == "/remote/program/upload":
            self.handle_program_upload(parsed)
            return
        self.write_json(404, {"ok": False, "error": "unsupported_remote_path"})

    def do_DELETE(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        self._auth_session = None
        if parsed.path.startswith("/remote/"):
            if not self.check_peer() or not self.authorize_remote("DELETE", parsed):
                return
        if parsed.path == "/remote/program/file":
            self.handle_program_file_delete(parsed)
            return
        self.write_json(404, {"ok": False, "error": "unsupported_remote_path"})

    def handle_info(self) -> None:
        input_enabled = os.environ.get("V5_UI_REMOTE_INPUT", "off") == "layout_only"
        self.write_json(200, {
            "protocol_version": PROTOCOL_VERSION,
            "ui_ready": True,
            "startup_status": "ready",
            "ready_metadata": self._request_ready_metadata,
            "frame_id": self.state.frame_id,
            "width": self.state.width,
            "height": self.state.height,
            "pixel_format": PIXEL_FORMAT,
            "stride": self.state.stride,
            "view_only": not input_enabled,
            "input_enabled": input_enabled,
            "system_metrics": system_metrics(
                self.state.display_cpu_metrics(),
                sample_cpu_if_missing=False,
            ),
            "program_api": "v5.remote_program.v1",
            "program_dir": str(self.program_service.root),
            "auth_protocol": "v5.remote.auth.v1",
            "device_id": self.server.auth_store.device_id,
        })

    def handle_program_list(self) -> None:
        try:
            self.write_json(200, self.program_service.list_files())
        except ProgramApiError as exc:
            self.write_program_error(exc)

    def handle_program_file_get(self, parsed: urllib.parse.ParseResult) -> None:
        query = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)
        file_name = self.query_value(query, "filename")
        try:
            if self.query_value(query, "content") in {"1", "true"}:
                result = self.program_service.read_file(file_name)
            else:
                result = self.program_service.stat_file(file_name)
            self.write_json(200, result)
        except ProgramApiError as exc:
            self.write_program_error(exc)

    def handle_program_upload(self, parsed: urllib.parse.ParseResult) -> None:
        query = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)
        file_name = self.query_value(query, "filename")
        overwrite = self.query_value(query, "overwrite") in {"1", "true"}
        try:
            content_length_text = self.headers.get("Content-Length")
            if content_length_text is None:
                raise ProgramApiError(411, "program_content_length_required", "上传 G-code 必须提供 Content-Length。")
            try:
                content_length = int(content_length_text)
            except ValueError as exc:
                raise ProgramApiError(400, "program_content_length_invalid", "上传 G-code 的 Content-Length 无效。") from exc
            if content_length <= 0:
                raise ProgramApiError(400, "program_file_empty", "不能上传空的 G-code 文件。")
            if content_length > MAX_GCODE_BYTES:
                raise ProgramApiError(413, "program_file_size_limit_exceeded", "G-code 文件超过板端 2 MiB 上限。")
            payload = self.rfile.read(content_length)
            if len(payload) != content_length:
                raise ProgramApiError(400, "program_upload_incomplete", "上传连接提前结束，板端未收到完整文件。")
            result = self.program_service.upload(
                file_name,
                payload,
                self.headers.get("X-8ax-File-Sha256", ""),
                overwrite,
            )
            self.write_json(200, result)
        except ProgramApiError as exc:
            self.write_program_error(exc)

    def handle_program_file_delete(self, parsed: urllib.parse.ParseResult) -> None:
        query = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)
        file_name = self.query_value(query, "filename")
        try:
            self.write_json(200, self.program_service.delete(file_name))
        except ProgramApiError as exc:
            self.write_program_error(exc)

    @staticmethod
    def query_value(query: dict[str, list[str]], key: str) -> str:
        values = query.get(key)
        return values[0] if values else ""

    def write_program_error(self, error: ProgramApiError) -> None:
        self.write_json(error.status, {
            "ok": False,
            "error": error.code,
            "message": error.message,
        })

    def handle_full_frame(self) -> None:
        if not self.request_ready_identity_is_current():
            self.write_json(503, {"ok": False, "error": "ui_identity_changed"})
            return
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
        if not self.request_ready_identity_is_current():
            self.write_json(503, {"ok": False, "error": "ui_identity_changed"})
            return
        if not self.accept_websocket():
            return
        self.state.mark_metric("stream_sessions")
        self.state.mark_metric("stream_active_sessions")
        sock = self.connection
        subscribed = False
        stream_stop = threading.Event()
        stream_send_lock = threading.Lock()
        stream_receiver = None
        waiting_for_post_repair_delta = False
        try:
            frame = self.state.full_frame()
            if frame is None:
                return
            frame_id, payload = frame
            self.state.mark_metric("stream_initial_full_frames")
            with stream_send_lock:
                self.send_frame(sock, frame_metadata("full_frame", frame_id, 0, self.state.width, self.state.height, self.state.stride, []), payload)
            last_sent = frame_id
            self.server.payload_producer.subscribe(frame_id)
            subscribed = True
            stream_receiver = threading.Thread(
                target=stream_ws_receive_loop,
                args=(
                    sock,
                    stream_stop,
                    stream_send_lock,
                    self.server.payload_producer.wake_waiters,
                    self.state.mark_metric,
                ),
                name="v5-ui-stream-rx",
                daemon=True,
            )
            stream_receiver.start()
            while not stream_stop.is_set():
                if not self.server.auth_store.session_is_current(self._auth_session):
                    self.state.mark_metric("stream_auth_expired_disconnects")
                    return
                if not self.request_ready_identity_is_current():
                    self.state.mark_metric("stream_runtime_resets")
                    return
                delivery = self.server.payload_producer.wait_after(last_sent,
                    STREAM_IDLE_PING_SECONDS,
                    cancel_event=stream_stop,
                )
                if stream_stop.is_set():
                    return
                if delivery is None:
                    with stream_send_lock:
                        send_ws_frame(sock, 0x9, b"")
                    self.state.mark_metric("stream_idle_pings")
                    continue
                if delivery.restart_stream:
                    self.state.mark_metric("stream_runtime_resets")
                    if delivery.reason == "invalid_dirty_event":
                        self.state.mark_metric("stream_runtime_invalid_dirty_disconnects")
                    else:
                        self.state.mark_metric("stream_runtime_unrepairable_disconnects")
                    return
                prepared = delivery.frame
                delivery_action = post_repair_delivery_action(
                    waiting_for_post_repair_delta,
                    delivery.repair_target_frame_id,
                    prepared is not None,
                )
                if delivery_action == "disconnect":
                    self.state.mark_metric("stream_runtime_slow_client_disconnects")
                    return
                if delivery_action == "repair":
                    repaired = self.server.payload_producer.prepare_full_repair(
                        delivery.repair_target_frame_id,
                        cancel_event=stream_stop,
                    )
                    if stream_stop.is_set():
                        return
                    if repaired is None:
                        self.state.mark_metric("stream_runtime_full_repair_failures")
                        return
                    with stream_send_lock:
                        self.send_prepared_full_frame(sock, repaired)
                    last_sent = repaired.frame_id
                    waiting_for_post_repair_delta = True
                    self.state.mark_metric("stream_repair_full_frames")
                    self.state.mark_metric("stream_runtime_full_repairs")
                    if delivery.reason in ("client_backlog", "shared_full_repair"):
                        self.state.mark_metric("stream_runtime_backlog_full_repairs")
                    else:
                        self.state.mark_metric("stream_repair_missing_dirty_events")
                        self.state.mark_metric("stream_runtime_history_gap_full_repairs")
                    continue
                if delivery_action == "continue":
                    continue
                with stream_send_lock:
                    self.send_prepared_frame(sock, prepared, delivery.override_base_frame_id)
                last_sent = prepared.frame_id
                if waiting_for_post_repair_delta:
                    self.state.mark_metric("stream_runtime_post_repair_recoveries")
                    waiting_for_post_repair_delta = False
                if delivery.override_base_frame_id > 0:
                    self.state.mark_metric("stream_runtime_covering_deltas")
        except (BrokenPipeError, ConnectionError, ConnectionResetError, OSError):
            self.state.mark_metric("stream_send_failures")
        finally:
            stream_stop.set()
            self.server.payload_producer.wake_waiters()
            if stream_receiver is not None and stream_receiver.is_alive():
                try:
                    sock.shutdown(socket.SHUT_RD)
                except OSError:
                    pass
                stream_receiver.join(0.25)
            if subscribed:
                self.server.payload_producer.unsubscribe()
            self.state.mark_metric("stream_disconnects")
            self.state.decrement_metric_floor("stream_active_sessions")

    def handle_input(self) -> None:
        if not self.request_ready_identity_is_current():
            self.write_json(503, {"ok": False, "error": "ui_identity_changed"})
            return
        if not self.accept_websocket():
            return
        self.state.mark_metric("input_sessions")
        self.state.mark_metric("input_active_sessions")
        input_enabled = os.environ.get("V5_UI_REMOTE_INPUT", "off") == "layout_only"
        session_id = ""
        sock = self.connection
        try:
            while True:
                if not self.server.auth_store.session_is_current(self._auth_session):
                    self.send_ack(sock, "pointer_reject", session_id, 0, "unknown", False, "remote_session_expired")
                    return
                if not self.request_ready_identity_is_current():
                    return
                opcode, payload = recv_ws_frame(sock)
                frame_action = input_ws_frame_action(opcode)
                if frame_action == "close":
                    return
                if frame_action == "pong":
                    send_ws_frame(sock, 0xA, payload)
                    continue
                if frame_action == "continue":
                    continue
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

    def request_ready_identity_is_current(self) -> bool:
        expected = getattr(self, "_request_ready_metadata", None)
        if not isinstance(expected, dict):
            return False
        current = self.state.ready_metadata()
        if current is None:
            return False
        keys = ("boot_id", "ui_instance_id", "ui_pid", "ui_start_ticks")
        return all(current.get(key) == expected.get(key) for key in keys)

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

    def send_prepared_frame(
        self,
        sock: socket.socket,
        prepared: PreparedDirtyFrame,
        override_base_frame_id: int = 0,
    ) -> None:
        meta_bytes = prepared.meta_bytes
        if override_base_frame_id > 0 and override_base_frame_id != prepared.base_frame_id:
            meta = json.loads(meta_bytes)
            meta["base_frame_id"] = int(override_base_frame_id)
            meta_bytes = json.dumps(meta, separators=(",", ":")).encode("utf-8")
        send_ws_frame(sock, 0x1, meta_bytes)
        send_ws_frame(sock, 0x2, prepared.payload)

    def send_prepared_full_frame(self, sock: socket.socket, prepared: PreparedFullFrame) -> None:
        send_ws_frame(sock, 0x1, prepared.meta_bytes)
        send_ws_frame(sock, 0x2, prepared.payload)

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
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(payload)

    def write_frame_envelope(self, meta_bytes: bytes, payload: FramePayload) -> None:
        meta_len = struct.pack("<I", len(meta_bytes))
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(meta_len) + len(meta_bytes) + len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(meta_len)
        self.wfile.write(meta_bytes)
        self.wfile.write(payload)


class RemoteRelayServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, address, handler, state: FrameState, allow_networks,
                 program_service: ProgramFileService, auth_store: AuthStore,
                 tls_context) -> None:
        super().__init__(address, handler)
        self.socket = tls_context.wrap_socket(self.socket, server_side=True)
        self.state = state
        self.allow_networks = allow_networks
        self.program_service = program_service
        self.auth_store = auth_store
        self.payload_producer = SharedDirtyPayloadProducer(state)
        self.payload_producer.start()

    def server_close(self) -> None:
        try:
            self.payload_producer.stop()
        finally:
            super().server_close()


def main() -> int:
    parser = argparse.ArgumentParser(description="v5 remote UI relay using UI mmap/FIFO IPC.")
    parser.add_argument("--host", default=os.environ.get("V5_UI_REMOTE_BIND", "0.0.0.0"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("V5_UI_REMOTE_PORT", "18080")))
    parser.add_argument("--allow-cidrs", default=os.environ.get("V5_UI_REMOTE_ALLOW_CIDRS", "127.0.0.1/32,192.168.1.0/24"))
    parser.add_argument("--run-dir", default=str(RUN_DIR))
    parser.add_argument("--width", type=int, default=int(os.environ.get("V5_UI_REMOTE_WIDTH", "1024")))
    parser.add_argument("--height", type=int, default=int(os.environ.get("V5_UI_REMOTE_HEIGHT", "600")))
    parser.add_argument("--ready-path", default=os.environ.get("V5_UI_READY_PATH", str(RUN_DIR / "ui_ready.json")))
    parser.add_argument("--program-root", default=os.environ.get("V5_GCODE_PROGRAM_ROOT", "/opt/8ax/v5/gcode/golden"))
    parser.add_argument("--tls-cert", default=os.environ.get("V5_UI_REMOTE_TLS_CERT", "/etc/6x-cnc/remote-relay/server-cert.pem"))
    parser.add_argument("--tls-key", default=os.environ.get("V5_UI_REMOTE_TLS_KEY", "/etc/6x-cnc/remote-relay/server-key.pem"))
    parser.add_argument("--auth-clients", default=os.environ.get("V5_UI_REMOTE_AUTH_CLIENTS", "/etc/6x-cnc/remote-relay/clients.json"))
    args = parser.parse_args()

    auth_store = AuthStore.from_file(Path(args.auth_clients))
    tls_context = build_server_tls_context(
        Path(args.tls_cert), Path(args.tls_key), auth_store.device_id)
    state = FrameState(
        Path(args.run_dir), args.width, args.height, NativeDirtyPacker(), Path(args.ready_path))
    dirty_reader = DirtyReader(state)
    dirty_reader.start()
    allow_networks = parse_allow_cidrs(args.allow_cidrs)
    program_service = ProgramFileService(args.program_root)
    with RemoteRelayServer(
        (args.host, args.port), RemoteRelayHandler, state, allow_networks,
        program_service, auth_store, tls_context,
    ) as server:
        print(f"v5_remote_ui_relay TLS listening host={args.host} port={args.port} device_id={auth_store.device_id} run_dir={state.run_dir} ready_path={state.ready_path} allow_cidrs={args.allow_cidrs}", flush=True)
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
