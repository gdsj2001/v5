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

from v5_remote_ui_relay_access import (
    RemoteRelayAccessMixin, remote_path_requires_ui, required_remote_scope,
    url_contains_auth_token,
)
from v5_remote_ui_relay_stream import (
    RemoteRelayStreamMixin, input_ws_frame_action, post_repair_delivery_action,
    stream_ws_receive_loop,
)
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













class RemoteRelayHandler(
    RemoteRelayAccessMixin,
    RemoteRelayStreamMixin,
    BaseHTTPRequestHandler,
):
    server_version = "V5RemoteUiRelay/1"



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
