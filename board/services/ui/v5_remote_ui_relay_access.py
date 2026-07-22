from __future__ import annotations

import ipaddress
import json
import os
import sys
import urllib.parse

from v5_remote_ui_auth import AuthError
from v5_remote_ui_contract import PIXEL_FORMAT, PROTOCOL_VERSION
from v5_remote_ui_programs import MAX_GCODE_BYTES, ProgramApiError, ProgramFileService
from v5_remote_ui_protocol import frame_metadata
from v5_remote_ui_state import FrameState
from v5_remote_ui_support import (
    cpu_samples_snapshot,
    peer_allowed,
    process_diagnostics,
    system_metrics,
)


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


class RemoteRelayAccessMixin:
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
