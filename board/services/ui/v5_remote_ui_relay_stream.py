from __future__ import annotations

import json
import os
import socket
import struct
import threading

from v5_remote_ui_contract import STREAM_IDLE_PING_SECONDS, FramePayload
from v5_remote_ui_protocol import (
    frame_metadata,
    recv_ws_frame,
    send_ws_frame,
    ws_accept_value,
)
from v5_remote_ui_shared_payload import PreparedDirtyFrame, PreparedFullFrame
from v5_remote_ui_support import now_ms


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


class RemoteRelayStreamMixin:
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
