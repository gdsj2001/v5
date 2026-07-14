#!/usr/bin/env python3
"""AI-only loopback browser bridge for the V5 board remote UI relay."""

from __future__ import annotations

import argparse
from pathlib import Path

from aiohttp import ClientSession, WSMsgType, web


VIEWER_HTML = r"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>V5 开发板屏幕</title>
  <style>
    :root { color-scheme: dark; font-family: system-ui, sans-serif; }
    body { margin: 0; background: #071722; color: #dcecf5; display: grid; place-items: center; min-height: 100vh; }
    main { width: min(100vw, 1048px); padding: 12px; box-sizing: border-box; }
    header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; }
    h1 { font-size: 16px; margin: 0; font-weight: 600; }
    #status { color: #67e8b3; font: 13px ui-monospace, monospace; }
    canvas { display: block; width: 100%; height: auto; background: #000; border: 1px solid #31566b; touch-action: none; cursor: default; }
  </style>
</head>
<body>
<main>
  <header><h1>V5 开发板实时屏幕</h1><div id="status">连接中…</div></header>
  <canvas id="screen" width="1024" height="600"></canvas>
</main>
<script>
(() => {
  const canvas = document.getElementById('screen');
  const ctx = canvas.getContext('2d', {alpha: false});
  const statusNode = document.getElementById('status');
  let rgba = null;
  let frameId = 0;
  let streamLive = false;
  let inputReady = false;
  let pendingMeta = null;
  let inputWs = null;
  let inputSeq = 0;
  let pointerDown = false;
  const sessionId = `v5-browser-${Date.now()}`;

  const wsUrl = path => `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}${path}`;
  const updateStatus = extra => {
    const parts = [`frame=${frameId}`, streamLive ? 'stream=live' : 'stream=connecting', inputReady ? 'input=ready' : 'input=waiting'];
    if (extra) parts.push(extra);
    statusNode.textContent = parts.join('  ');
  };

  function bgraRowToRgba(source, sourceOffset, target, targetOffset, pixelCount) {
    for (let x = 0; x < pixelCount; x++) {
      const s = sourceOffset + x * 4;
      const d = targetOffset + x * 4;
      target[d] = source[s + 2];
      target[d + 1] = source[s + 1];
      target[d + 2] = source[s];
      target[d + 3] = source[s + 3];
    }
  }

  function applyFull(meta, pixels) {
    canvas.width = meta.width;
    canvas.height = meta.height;
    rgba = new Uint8ClampedArray(meta.width * meta.height * 4);
    for (let y = 0; y < meta.height; y++) {
      bgraRowToRgba(pixels, y * meta.stride, rgba, y * meta.width * 4, meta.width);
    }
    ctx.putImageData(new ImageData(rgba, meta.width, meta.height), 0, 0);
    frameId = Number(meta.frame_id || 0);
    updateStatus();
  }

  function applyDirty(meta, pixels) {
    if (!rgba || Number(meta.base_frame_id) !== frameId) {
      repairFull('base-repair');
      return;
    }
    let payloadOffset = 0;
    for (const rect of meta.rects || []) {
      const rectRgba = new Uint8ClampedArray(rect.w * rect.h * 4);
      for (let y = 0; y < rect.h; y++) {
        const packedRow = payloadOffset + y * rect.w * 4;
        bgraRowToRgba(pixels, packedRow, rectRgba, y * rect.w * 4, rect.w);
        rgba.set(rectRgba.subarray(y * rect.w * 4, (y + 1) * rect.w * 4), ((rect.y + y) * canvas.width + rect.x) * 4);
      }
      ctx.putImageData(new ImageData(rectRgba, rect.w, rect.h), rect.x, rect.y);
      payloadOffset += rect.w * rect.h * 4;
    }
    frameId = Number(meta.frame_id || frameId);
    updateStatus();
  }

  async function repairFull(reason = '') {
    try {
      const response = await fetch('/api/frame', {cache: 'no-store'});
      if (!response.ok) throw new Error(`full frame HTTP ${response.status}`);
      const bytes = new Uint8Array(await response.arrayBuffer());
      const metaLen = new DataView(bytes.buffer, bytes.byteOffset, 4).getUint32(0, true);
      const meta = JSON.parse(new TextDecoder().decode(bytes.subarray(4, 4 + metaLen)));
      applyFull(meta, bytes.subarray(4 + metaLen));
      updateStatus(reason);
    } catch (error) {
      updateStatus(`frame-error=${error.message}`);
    }
  }

  function connectStream() {
    const ws = new WebSocket(wsUrl('/api/stream'));
    ws.binaryType = 'arraybuffer';
    ws.onopen = () => { streamLive = true; updateStatus(); };
    ws.onmessage = event => {
      if (typeof event.data === 'string') {
        pendingMeta = JSON.parse(event.data);
        return;
      }
      if (!pendingMeta) return;
      const pixels = new Uint8Array(event.data);
      const meta = pendingMeta;
      pendingMeta = null;
      if (meta.type === 'full_frame') applyFull(meta, pixels);
      else if (meta.type === 'dirty_rects') applyDirty(meta, pixels);
    };
    ws.onerror = () => updateStatus('stream-error');
    ws.onclose = () => {
      streamLive = false;
      pendingMeta = null;
      updateStatus('stream-reconnect');
      setTimeout(() => { repairFull('reconnected'); connectStream(); }, 1000);
    };
  }

  function sendInput(message) {
    if (inputWs && inputWs.readyState === WebSocket.OPEN) inputWs.send(JSON.stringify(message));
  }

  function connectInput() {
    inputWs = new WebSocket(wsUrl('/api/input'));
    inputWs.onopen = () => sendInput({
      type: 'control_request', session_id: sessionId, source: 'v5_browser_mirror', client_time_ms: Date.now()
    });
    inputWs.onmessage = event => {
      const message = JSON.parse(event.data);
      if (message.type === 'control_grant') inputReady = Boolean(message.accepted);
      updateStatus(message.reason || '');
    };
    inputWs.onclose = () => { inputReady = false; updateStatus('input-reconnect'); setTimeout(connectInput, 1000); };
    inputWs.onerror = () => updateStatus('input-error');
  }

  function boardPoint(event) {
    const bounds = canvas.getBoundingClientRect();
    return {
      x: Math.max(0, Math.min(canvas.width - 1, Math.floor((event.clientX - bounds.left) * canvas.width / bounds.width))),
      y: Math.max(0, Math.min(canvas.height - 1, Math.floor((event.clientY - bounds.top) * canvas.height / bounds.height)))
    };
  }

  function pointerEvent(phase, event) {
    if (!inputReady) return;
    const point = boardPoint(event);
    sendInput({
      type: 'pointer_event', session_id: sessionId, source: 'v5_browser_mirror', seq: ++inputSeq,
      phase, x: point.x, y: point.y, button: 'left', client_time_ms: Date.now()
    });
  }

  canvas.addEventListener('pointerdown', event => {
    event.preventDefault();
    pointerDown = true;
    canvas.setPointerCapture(event.pointerId);
    pointerEvent('down', event);
  });
  canvas.addEventListener('pointermove', event => { if (pointerDown) pointerEvent('move', event); });
  canvas.addEventListener('pointerup', event => {
    if (!pointerDown) return;
    pointerEvent('up', event);
    pointerDown = false;
  });
  canvas.addEventListener('pointercancel', event => {
    if (pointerDown) pointerEvent('up', event);
    pointerDown = false;
  });

  repairFull('initial');
  connectStream();
  connectInput();
})();
</script>
</body>
</html>
"""


async def index(_: web.Request) -> web.Response:
    return web.Response(text=VIEWER_HTML, content_type="text/html")


async def proxy_http(request: web.Request) -> web.Response:
    session: ClientSession = request.app["client"]
    board_base = request.app["board_base"]
    board_path = "/remote/info" if request.match_info["kind"] == "info" else "/remote/frame/full"
    async with session.get(board_base + board_path) as response:
        payload = await response.read()
        return web.Response(body=payload, status=response.status, content_type=response.content_type)


async def proxy_websocket(request: web.Request) -> web.WebSocketResponse:
    browser_ws = web.WebSocketResponse(heartbeat=20.0)
    await browser_ws.prepare(request)
    session: ClientSession = request.app["client"]
    board_ws_base = request.app["board_ws_base"]
    board_path = "/remote/stream" if request.match_info["kind"] == "stream" else "/remote/input"
    async with session.ws_connect(board_ws_base + board_path, heartbeat=20.0, max_msg_size=8 * 1024 * 1024) as board_ws:
        async def browser_to_board() -> None:
            async for message in browser_ws:
                if message.type == WSMsgType.TEXT:
                    await board_ws.send_str(message.data)
                elif message.type == WSMsgType.BINARY:
                    await board_ws.send_bytes(message.data)
                elif message.type in (WSMsgType.CLOSE, WSMsgType.ERROR):
                    break

        async def board_to_browser() -> None:
            async for message in board_ws:
                if message.type == WSMsgType.TEXT:
                    await browser_ws.send_str(message.data)
                elif message.type == WSMsgType.BINARY:
                    await browser_ws.send_bytes(message.data)
                elif message.type in (WSMsgType.CLOSE, WSMsgType.ERROR):
                    break

        import asyncio
        tasks = [asyncio.create_task(browser_to_board()), asyncio.create_task(board_to_browser())]
        done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
        for task in pending:
            task.cancel()
        for task in done:
            task.result()
    await browser_ws.close()
    return browser_ws


async def create_app(board_host: str, board_port: int) -> web.Application:
    app = web.Application()
    app["board_base"] = f"http://{board_host}:{board_port}"
    app["board_ws_base"] = f"ws://{board_host}:{board_port}"
    app["client"] = ClientSession()
    app.router.add_get("/", index)
    app.router.add_get("/api/{kind:info|frame}", proxy_http)
    app.router.add_get("/api/{kind:stream|input}", proxy_websocket)

    async def cleanup(application: web.Application) -> None:
        await application["client"].close()

    app.on_cleanup.append(cleanup)
    return app


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--board-host", default="192.168.1.221")
    parser.add_argument("--board-port", type=int, default=18080)
    parser.add_argument("--listen", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18777)
    args = parser.parse_args()
    web.run_app(create_app(args.board_host, args.board_port), host=args.listen, port=args.port, print=None)


if __name__ == "__main__":
    main()
