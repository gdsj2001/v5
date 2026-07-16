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
    #screen-wrap { position: relative; width: 100%; aspect-ratio: 1024 / 600; background: #000; border: 1px solid #31566b; touch-action: none; cursor: default; }
    #screen-wrap canvas { position: absolute; inset: 0; display: block; width: 100%; height: 100%; pointer-events: none; }
  </style>
</head>
<body>
<main>
  <header><h1>V5 开发板实时屏幕</h1><div id="status">连接中…</div></header>
  <div id="screen-wrap">
    <canvas id="screen" width="1024" height="600"></canvas>
  </div>
</main>
<script>
(() => {
  const screenTarget = document.getElementById('screen-wrap');
  const canvas = document.getElementById('screen');
  const statusNode = document.getElementById('status');
  const bitmapCtx = canvas.getContext('bitmaprenderer');
  if (!bitmapCtx || typeof OffscreenCanvas !== 'function') {
    statusNode.textContent = 'render=unsupported';
    return;
  }
  let stagingCanvas = new OffscreenCanvas(1024, 600);
  let stagingCtx = stagingCanvas.getContext('2d', {alpha: false});
  if (!stagingCtx) {
    statusNode.textContent = 'render=unsupported';
    return;
  }
  let rgba = null;
  let frameId = 0;
  let streamLive = false;
  let inputReady = false;
  let pendingMeta = null;
  let streamWs = null;
  let streamGeneration = 0;
  let reconnectTimer = null;
  let baseReady = false;
  let progressCheckInFlight = false;
  let upstreamAheadSince = 0;
  let inputWs = null;
  let inputSeq = 0;
  let pointerDown = false;
  let pointerForwarded = false;
  let aiClickActive = false;
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

  function presentRgba(width, height) {
    if (stagingCanvas.width !== width || stagingCanvas.height !== height) {
      stagingCanvas = new OffscreenCanvas(width, height);
      stagingCtx = stagingCanvas.getContext('2d', {alpha: false});
      if (!stagingCtx) throw new Error('staging context unavailable');
    }
    stagingCtx.putImageData(new ImageData(rgba, width, height), 0, 0);
    const bitmap = stagingCanvas.transferToImageBitmap();
    if (canvas.width !== width || canvas.height !== height) {
      canvas.width = width;
      canvas.height = height;
    }
    bitmapCtx.transferFromImageBitmap(bitmap);
  }

  function applyFull(meta, pixels, generation) {
    const width = Number(meta.width || 0);
    const height = Number(meta.height || 0);
    const stride = Number(meta.stride || 0);
    const nextFrameId = Number(meta.frame_id || 0);
    if (generation !== streamGeneration || width <= 0 || height <= 0 || stride < width * 4 ||
        nextFrameId <= 0 || pixels.byteLength < stride * height) {
      scheduleStreamReconnect('invalid-full-frame');
      return;
    }
    const nextRgba = new Uint8ClampedArray(width * height * 4);
    for (let y = 0; y < height; y++) {
      bgraRowToRgba(pixels, y * stride, nextRgba, y * width * 4, width);
    }
    if (generation !== streamGeneration) return;
    rgba = nextRgba;
    presentRgba(width, height);
    frameId = nextFrameId;
    baseReady = true;
    streamLive = true;
    upstreamAheadSince = 0;
    updateStatus();
  }

  function applyDirty(meta, pixels, generation) {
    if (generation !== streamGeneration) return;
    if (!baseReady || !rgba || Number(meta.base_frame_id) !== frameId) {
      scheduleStreamReconnect('base-repair');
      return;
    }
    let expectedBytes = 0;
    for (const rect of meta.rects || []) {
      if (rect.x < 0 || rect.y < 0 || rect.w <= 0 || rect.h <= 0 ||
          rect.x + rect.w > canvas.width || rect.y + rect.h > canvas.height) {
        scheduleStreamReconnect('invalid-dirty-rect');
        return;
      }
      expectedBytes += rect.w * rect.h * 4;
    }
    if (expectedBytes !== pixels.byteLength) {
      scheduleStreamReconnect('invalid-dirty-payload');
      return;
    }
    let payloadOffset = 0;
    for (const rect of meta.rects || []) {
      for (let y = 0; y < rect.h; y++) {
        const packedRow = payloadOffset + y * rect.w * 4;
        const targetRow = ((rect.y + y) * canvas.width + rect.x) * 4;
        bgraRowToRgba(pixels, packedRow, rgba, targetRow, rect.w);
      }
      payloadOffset += rect.w * rect.h * 4;
    }
    presentRgba(canvas.width, canvas.height);
    frameId = Number(meta.frame_id || frameId);
    upstreamAheadSince = 0;
    updateStatus();
  }

  async function checkStreamProgress() {
    if (progressCheckInFlight || !streamLive || !baseReady) return;
    progressCheckInFlight = true;
    const generation = streamGeneration;
    try {
      const response = await fetch('/api/info', {cache: 'no-store'});
      if (!response.ok) throw new Error(`info HTTP ${response.status}`);
      const info = await response.json();
      if (generation !== streamGeneration) return;
      const upstreamFrameId = Number(info.frame_id || 0);
      if (upstreamFrameId > frameId) {
        if (!upstreamAheadSince) upstreamAheadSince = Date.now();
        else if (Date.now() - upstreamAheadSince >= 1000) scheduleStreamReconnect('stale-stream');
      } else {
        upstreamAheadSince = 0;
      }
    } catch (error) {
      if (generation === streamGeneration) updateStatus(`progress-error=${error.message}`);
    } finally {
      progressCheckInFlight = false;
    }
  }

  function scheduleStreamReconnect(reason) {
    streamLive = false;
    baseReady = false;
    upstreamAheadSince = 0;
    pendingMeta = null;
    updateStatus(reason);
    const oldWs = streamWs;
    streamWs = null;
    streamGeneration += 1;
    if (oldWs && oldWs.readyState < WebSocket.CLOSING) oldWs.close();
    if (reconnectTimer !== null) return;
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      connectStream();
    }, 250);
  }

  function connectStream() {
    const generation = ++streamGeneration;
    const ws = new WebSocket(wsUrl('/api/stream'));
    streamWs = ws;
    ws.binaryType = 'arraybuffer';
    ws.onopen = () => {
      if (generation === streamGeneration) updateStatus('stream-open');
    };
    ws.onmessage = event => {
      if (generation !== streamGeneration) return;
      if (typeof event.data === 'string') {
        pendingMeta = JSON.parse(event.data);
        return;
      }
      if (!pendingMeta) return;
      const pixels = new Uint8Array(event.data);
      const meta = pendingMeta;
      pendingMeta = null;
      if (meta.type === 'full_frame') applyFull(meta, pixels, generation);
      else if (meta.type === 'dirty_rects') applyDirty(meta, pixels, generation);
    };
    ws.onerror = () => {
      if (generation === streamGeneration) updateStatus('stream-error');
    };
    ws.onclose = () => {
      if (generation !== streamGeneration) return;
      streamWs = null;
      streamLive = false;
      baseReady = false;
      pendingMeta = null;
      updateStatus('stream-reconnect');
      if (reconnectTimer === null) {
        reconnectTimer = setTimeout(() => {
          reconnectTimer = null;
          connectStream();
        }, 1000);
      }
    };
  }

  function sendInput(message) {
    if (!inputWs || inputWs.readyState !== WebSocket.OPEN) return false;
    inputWs.send(JSON.stringify(message));
    return true;
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
    const bounds = screenTarget.getBoundingClientRect();
    return {
      x: Math.max(0, Math.min(canvas.width - 1, Math.floor((event.clientX - bounds.left) * canvas.width / bounds.width))),
      y: Math.max(0, Math.min(canvas.height - 1, Math.floor((event.clientY - bounds.top) * canvas.height / bounds.height)))
    };
  }

  function sendPointer(phase, x, y, requireRenderable) {
    if (!inputReady || (requireRenderable && (!streamLive || !baseReady))) return false;
    return sendInput({
      type: 'pointer_event', session_id: sessionId, source: 'v5_browser_mirror', seq: ++inputSeq,
      phase, x, y, button: 'left', client_time_ms: Date.now()
    });
  }

  function pointerEvent(phase, event, requireRenderable = true) {
    const point = boardPoint(event);
    return sendPointer(phase, point.x, point.y, requireRenderable);
  }

  async function v5AiClick(x, y, holdMs = 60) {
    const point = {
      x: Math.max(0, Math.min(canvas.width - 1, Math.floor(Number(x)))),
      y: Math.max(0, Math.min(canvas.height - 1, Math.floor(Number(y))))
    };
    if (aiClickActive || pointerDown) return {ok: false, reason: 'pointer_busy'};
    if (!inputReady || !streamLive || !baseReady) return {ok: false, reason: 'not_ready'};
    aiClickActive = true;
    try {
      if (!sendPointer('down', point.x, point.y, true)) return {ok: false, reason: 'down_not_sent'};
      await new Promise(resolve => setTimeout(resolve, Math.max(40, Math.min(500, Number(holdMs) || 60))));
      if (!sendPointer('up', point.x, point.y, false)) return {ok: false, reason: 'up_not_sent'};
      return {ok: true, x: point.x, y: point.y};
    } finally {
      aiClickActive = false;
    }
  }

  globalThis.v5AiClick = v5AiClick;

  screenTarget.addEventListener('pointerdown', event => {
    event.preventDefault();
    pointerDown = true;
    screenTarget.setPointerCapture(event.pointerId);
    pointerForwarded = pointerEvent('down', event, true);
  });
  screenTarget.addEventListener('pointermove', event => {
    if (pointerDown && pointerForwarded) pointerEvent('move', event, false);
  });
  screenTarget.addEventListener('pointerup', event => {
    if (!pointerDown) return;
    if (pointerForwarded) pointerEvent('up', event, false);
    pointerDown = false;
    pointerForwarded = false;
  });
  screenTarget.addEventListener('pointercancel', event => {
    if (pointerDown && pointerForwarded) pointerEvent('up', event, false);
    pointerDown = false;
    pointerForwarded = false;
  });

  connectStream();
  connectInput();
  setInterval(checkStreamProgress, 1000);
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
