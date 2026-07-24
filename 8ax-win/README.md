# 8ax Windows Remote UI

Windows lightweight remote display client for the V5 board UI.

## Current Scope

Current slice:

- .NET 8 WPF application.
- 1024 x 600 remote framebuffer canvas.
- W2 protocol DTOs and dirty-rect frame assembly.
- W3 Windows-only certificate pinning and challenge/session/scope protocol tests.
- W4 authenticated HTTPS/WSS relay client.
- W5 runtime metrics and event evidence output.
- Formal board relay direction: board-side `remote_ui_relay` serves one full frame and LVGL flush dirty rectangles.
- FPS, frame id, capture/relay state, full/dirty/repair/reject counters, and mapped pointer coordinates.
- Top-bar board resource diagnostics from `/remote/info`: `cpu0`, `cpu1`, memory, and disk usage.
- Enlarged operator window with a bottom client-command row.
- Bottom `鐠囪褰囬弮銉ョ箶` button requests a read-only board runtime diagnostics snapshot from `/remote/diagnostics` for later troubleshooting.
- Bottom `娴肩嚌娴狅絿鐖渀 button opens a local file picker, checks the board program directory for a same-name file, asks before overwrite, and uploads the selected G-code file through `/remote/program/upload` into the board program-open directory.
- Bottom `閹垫挸绱戠化鑽ょ埠G娴狅絿鐖渀 button opens the board program directory in a local-directory style window backed by `/remote/program/list` and `/remote/program/file`; the operator can refresh, upload, open for edit, save, and delete board G-code files.
- Bottom `OTA閸楀洨楠嘸 button sends `POST /remote/ota/upgrade` to the board relay. Current board code returns `OTA_NOT_IMPLEMENTED` fail-closed until the board OTA client, Broker action, and VPS package selector exist.
- Default relay origin is `https://192.168.1.221:18080/`, but startup requires an explicitly selected device profile under `D:\授权私钥`; missing credentials fail closed.
- Formal relay input path: relay mode sends mouse down/up over authenticated `WSS /remote/input` and waits for board `pointer_ack`.
- Local verification console with no external NuGet dependency.
- No LinuxCNC, HAL, G-code execution, or motion command path. G-code transfer only writes the selected file into the board program directory through the relay.

Default double-click or no-argument launch is the only supported fast operator path. It must use `relay stream` plus `relay input`, with board touch ACK under 50ms and first Win-side visual feedback under 200ms. The old direct `/dev/fb0` capture path is retired because it can produce wrong colors and black regions.

The formal path is board `remote_ui_relay`: a single WebSocket initial full frame followed by the WebSocket dirty-rect stream sourced from LVGL flush rectangles, not an extra HTTP initialization request, high-frequency full-screen sampling, or raw fb0 dumps. The board UI and display projection remain 30Hz, while WinRemote relay stream presentation targets 10Hz, about 100ms per latest-wins dirty-rect frame; the client may throttle status and metrics logging to that cadence, but frame delivery remains WebSocket dirty-rect driven and must not switch to HTTP full-frame polling. If `/remote/info` and `/remote/frame/full` are reachable but `WS /remote/stream` cannot be upgraded or disconnects, WinRemote records `relay_stream_unavailable`, schedules a clean reconnect, and does not fall back to relay polling, FIFO, SSH tap, direct board files, or retired `/dev/fb0` capture.

`--board-fb0 true` is retired and must not select a capture mode. The formal
operator path is relay mode only: WinRemote sends pointer events over the relay
WebSocket, and the board relay injects them into the original board UI event
chain.

Relay input is the formal input path. It works with a board relay reporting `view_only=false`. The client sends `control_request`, then `pointer_event` down/up messages, records `pointer_ack`, and measures the first dirty-rect feedback after a down event as `relay_pointer_feedback`. If the stream connected while the board was temporarily `view_only`, the next operator click must retry `control_request` before giving up.
If a cached relay input WebSocket is closed or stale when an operator presses the mouse, WinRemote must discard that socket, request control again, and retry the same mouse-down once. Move and release events must not be blindly reordered or replayed after a failed send.

When relay stream and relay input are connected, the top-right connection badge
must stay in the live/connected state. The client must not show or schedule
`reconnecting` while the current relay session is healthy. After stream end,
transport error, or startup failure, the badge must stay as stable `error` while
retry attempts are shown only in the bottom status text and evidence.

If the dirty-rect WebSocket is unavailable, the connection badge must move to
stable error state. The stream carries its own initial full frame; HTTP
full-frame refresh is only the explicit repair path, not a degraded display mode.

The center of the top bar shows board system usage as `cpu0`, `cpu1`, memory,
and disk percentages. These values come only from the board relay's
`/remote/info.system_metrics` payload; the Windows client must not run board
commands, read board files, or infer product control state from these numbers.
If a relay does not provide metrics yet, the top bar must keep a compact `--%`
placeholder instead of blocking the remote display/input path. The metric labels
and top-bar geometry are static; refreshes may update only the four percentage
number fields.

On the first successful `/remote/info` handshake for a relay process,
WinRemote also sends the PC time to the board so the board can overwrite its
system clock. The client first tries to resolve network time from the existing
VPS HTTPS `Date` response headers; if that is unavailable or times out, it
falls back to the Windows local UTC clock. The board relay may set the system
clock once after startup and then must ignore later metric refreshes for time
sync until the relay process restarts.

When `tools/v3_pipeline.py` is used to verify a WinRemote or relay-facing
change, the pipeline must collect client-side evidence in addition to board
relay probes. A client pass requires `win_client_events.jsonl` or the generated
`winremote_client_probe.json` to show at least `relay_info`,
`relay_input_granted`, and `frame_applied`. `/remote/info`, TCP port 18080, or
board relay status alone only prove board relay readiness; they do not prove
the Windows client display/input path.

## File Ownership

| File | Step | Status |
| --- | --- | --- |
| `src/8ax.WinRemote/Config/AppSettings.cs` | W4/W8 | Used by `MainWindow` to select formal relay/input mode, evidence path, and formal remote input enablement; direct fb0 capture flags are retired and ignored. |
| `src/8ax.WinRemote/Config/RelaySecurityProfile.cs` | W3/W4 | Strict device profile loader, TLS 1.2+ certificate fingerprint/device-ID verifier, and HMAC canonicalizer. |
| `src/8ax.WinRemote/Protocol/RemoteMessage.cs` | W2/W8 | Shared message constants, info DTO, diagnostics/upload DTOs, frame packet DTO, and input control/ACK constants; used by tests, assembler, and transport. |
| `src/8ax.WinRemote/Protocol/FrameMetadata.cs` | W2 | `full_frame` and `dirty_rects` metadata; used by tests, assembler, and transport. |
| `src/8ax.WinRemote/Protocol/DirtyRectMetadata.cs` | W2 | Dirty rect DTO; used by tests and assembler. |
| `src/8ax.WinRemote/Protocol/PointerEventMessage.cs` | W2/W8 | Pointer, control request, and ACK DTOs; covered by serialization tests and sent by relay input mode. |
| `src/8ax.WinRemote/Protocol/RemoteProtocolJson.cs` | W2 | JSON and binary frame envelope helper; used by tests and transport. |
| `src/8ax.WinRemote/Rendering/PixelFormatConverter.cs` | W2 | Converts `bgra32` and `rgb565` to WPF BGRA32; covered by tests and used by assembler. |
| `src/8ax.WinRemote/Rendering/RemoteFramebuffer.cs` | W2 | Local backbuffer and dirty rect application; used by `MainWindow` relay mode and tests. |
| `src/8ax.WinRemote/Rendering/RemoteFrameAssembler.cs` | W2 | Validates frame/base ids, rect bounds, payload length, and applies full/dirty frames; used by `MainWindow` and tests. |
| `src/8ax.WinRemote/Transport/IRemoteTransport.cs` | W4 | Read-only transport contract; implemented by `RemoteRelayClient`. |
| `src/8ax.WinRemote/Transport/RelayAuthenticationSession.cs` | W3/W4 | One-time challenge-response, short RAM session cache, HTTP/WSS authorization headers, and bounded reauthentication. |
| `src/8ax.WinRemote/Transport/RemoteRelayClient.cs` | W4/W8 | Authenticated HTTPS diagnostics/program/full-frame client plus authenticated WSS dirty stream and remote input. |

## Commands

```powershell
dotnet restore .\8ax.WinRemote.sln --ignore-failed-sources
dotnet build .\8ax.WinRemote.sln --no-restore
dotnet run --project .\tests\8ax.WinRemote.Tests\8ax.WinRemote.Tests.csproj
dotnet run --project .\src\8ax.WinRemote\8ax.WinRemote.csproj -- --relay-security-profile D:\授权私钥\remote-relay-390529\winremote\relay-security.json
```

The verification console validates strict profile parsing, HTTPS-only routing, certificate fingerprint/device-ID checks, challenge HMAC canonicalization, scope mapping, and the frame assembler without introducing a plaintext test fallback.

Runtime evidence defaults to `%TEMP%\8ax-win\evidence` and can be redirected:

```powershell
.\publish\win-x64\8ax.WinRemote.exe --relay-security-profile D:\授权私钥\remote-relay-390529\winremote\relay-security.json --evidence-dir .\repo_ignored\evidence\manual_8ax_win
.\publish\win-x64\8ax.WinRemote.exe --relay-security-profile D:\授权私钥\remote-relay-390529\winremote\relay-security.json --view-only true --evidence-dir .\repo_ignored\evidence\manual_8ax_win
```

The client writes:

```text
win_client_events.jsonl
win_client_metrics.jsonl
```

The recorder redacts token, secret, password, and authorization fields.

The bottom `鐠囪褰囬弮銉ョ箶` button calls the board relay `GET /remote/diagnostics`.
The dialog must default to a concise Chinese diagnostic summary instead of raw
JSON. The summary shows: diagnostic time/id, overall state, UI service state,
remote display state, remote input state, state-sync state, command-channel
state, program-file availability, recent screen refresh, recent input, and
recent error status. A `鐠囷妇绮忔穱鈩冧紖` button may toggle indented, wrapped JSON for
maintenance use. Both summary and detail views must redact LinuxCNC/control
layer names, hardware/resource words, and direct low-level board paths before
display. The Windows client must not SSH to the board, shell out, read board
files directly, or use diagnostics data as control truth.

The bottom `娴肩嚌娴狅絿鐖渀 button opens a local file picker for `.ngc`, `.nc`, `.tap`,
and `.gcode` files, then sends the selected file body to the board relay
`POST /remote/program/upload?filename=<name>&overwrite=0`. Before upload the
client calls `GET /remote/program/file?filename=<name>`; if the board already
has that basename, the client must ask the operator before retrying with
`overwrite=1`. The relay validates the basename and extension, writes atomically
into `/opt/8ax/v5/gcode/golden`, the same directory scanned by the board
`打开程序` page, and returns size/SHA evidence. Upload success
does not open, run, preview, or select the program; it only makes the file
available to the board program list, which refreshes from the same directory.
Any later program-open/run action must still go through the existing board
UI/operator command path.

The G-code upload and Program Open single-file limit is exactly `64 MiB`
(`67,108,864 bytes`) across WinRemote, the relay Content-Length/body checks,
the board file service, and the board program runtime. Empty files and files
larger than that limit are rejected; the remote text-edit window keeps its
separate smaller edit limit.

The bottom `閹垫挸绱戠化鑽ょ埠G娴狅絿鐖渀 button opens a local-directory style board program
window. It uses `GET /remote/program/list` to display the current board G-code
files, `GET /remote/program/file?filename=<name>&content=1` to open a selected
file for editing, `POST /remote/program/upload?filename=<name>&overwrite=1` to
save edits or confirmed overwrite uploads, and `DELETE /remote/program/file?
filename=<name>` to delete a selected file after confirmation. The dialog is a
file-management and text-editing surface only; it must not execute G-code or
send motion/control commands.

The bottom `OTA閸楀洨楠嘸 button calls the board relay `POST /remote/ota/upgrade`.
WinRemote only sends the request and displays the returned status; it must not
download OTA packages to Windows, SSH/SFTP to the board, write rootfs/boot env,
or restart the board. The request declares the fixed package policy
`dna_private_first_no_public_when_private_present`: the board OTA client must
select a current-DNA private OTA package when one exists and must not download
the public OTA package in that case. With the current source state, the board
relay returns `status=rejected`, `code=OTA_NOT_IMPLEMENTED`, and
no job id.

## Upgrade

WinRemote accepts only signed VPS release sets. It verifies the raw
UTF-8-without-BOM `manifest.json` bytes with the embedded ECDSA P-256 public key
and detached DER `manifest.sig` before parsing or using any field. The fixed
identity is `schema=v5.winremote_update_manifest.v2` and
`key_id=winremote-update-p256-2026-01`. Unknown keys, malformed or duplicate
fields, a BOM, an invalid signature, or disagreeing primary/backup bytes are
rejected. SHA-256 is used only for the package identity bound by that trusted
manifest.

The top-bar `閸楀洨楠嘸 button checks these VPS manifests in order:

```text
https://license.cjwsjzyy.xyz/8ax-winremote/win-x64/manifest.json
https://license.cjwsjzyy.xyz/8ax-winremote/win-x64/manifest.sig
https://license.3dtouch.top/8ax-winremote/win-x64/manifest.json
https://license.3dtouch.top/8ax-winremote/win-x64/manifest.sig
```

The executable name must stay fixed across every update:

```text
8ax.WinRemote.exe
```

Publish the zip, manifest, and detached signature to:

```text
/var/www/html/updates/8ax-winremote/win-x64/
```

Use `tools/publish_winremote_update.ps1 -Version <version> -ReleaseSequence
<monotonic-integer>` for normal VPS releases. The signing private key remains
outside the repository at `D:\授权私钥\winremote-update-signing-private.pem` and
must never be copied to the client, VPS, board, VM, logs, or release package.
The script must not stop after local package generation: every version that
creates or replaces `publish\win-x64\8ax.WinRemote.exe` must upload all three
release files in the same run, read them back from the VPS, download both HTTPS
release sets, and verify raw-byte identity, signature, package size, and package
hash. `-SkipUpload` is allowed only when both publish and release output stay in
isolated `repo_ignored` directories; it must never update the formal publish
directory or be handed off as a client release.

Every WinRemote release is single-file only. The local publish directory and the
VPS update zip must contain only:

```text
8ax.WinRemote.exe
```

Do not publish `8ax.WinRemote.dll`, `.deps.json`, `.runtimeconfig.json`, `.pdb`,
or any other sidecar runtime files. The signed zip must contain exactly one
entry named `8ax.WinRemote.exe`.

The release version passed to `-Version` is written to both the signed manifest
and the generated executable metadata. `-ReleaseSequence` is a separate,
strictly increasing rollback barrier persisted only after a verified install.
The upgrade button compares the local executable version with the verified
manifest before downloading. If the server version is not newer, the client
does not download, install, or restart.

After download, the client verifies the signed size and SHA-256, copies the
current executable into a protected random directory, and launches that same
program with `--apply-update`; it never launches a replaceable PowerShell
installer or uses `ExecutionPolicy Bypass`. After the UI parent exits, the
installer verifies the original manifest/signature again, opens the zip with
`FileShare.None`, re-verifies size/hash on that same handle, checks the sole
executable product/version identity, atomically replaces the installed exe,
restarts it, and only then records the trusted release sequence. Failure leaves
the prior executable in place or rolls it back. Events are appended to
`winremote_update_events.jsonl` in the configured evidence directory.

On startup the client performs a read-only signed VPS manifest check. If the
server version is newer than the local executable, the fixed-size top-bar
upgrade button is shown; it must not download or install until the user clicks it.
If no newer server version exists, the top-bar upgrade button is hidden.
When the local version is already current, the upgrade dialog must say that no
download, install, or restart is needed; the download/restart warning is only
shown while an actual update is being prepared or installed.

Do not update the archived installed WinRemote directory by manually copying
over only one file. Local machines must be updated by launching
`8ax.WinRemote.exe` and using the top-bar `閸楀洨楠嘸 button, which downloads the
single-exe VPS package. The same generated exe can also be sent manually to
another Windows machine.

WinRemote is a single-instance operator terminal. Only one `8ax.WinRemote.exe`
process is allowed to own the remote display/input session on a Windows user
session. If the operator starts the executable again while one instance is
already running, the second process must show a short prompt and exit before it
opens another window or creates another relay connection.

Configuration can be supplied with `--config` or by placing `8ax.WinRemote.json` next to the published executable. Command-line options override the file.

```json
{
  "relay_url": "https://192.168.1.221:18080/",
  "relay_security_profile": "D:\\授权私钥\\remote-relay-390529\\winremote\\relay-security.json",
  "evidence_dir": ".\\repo_ignored\\evidence\\manual_8ax_win",
  "view_only": false,
  "enable_pointer": false,
  "enable_remote_input": true
}
```

```powershell
.\publish\win-x64\8ax.WinRemote.exe --config .\8ax.WinRemote.json
```

Connect only to the real board identity selected by the device profile:

```powershell
dotnet run --project .\src\8ax.WinRemote\8ax.WinRemote.csproj -- --relay-security-profile D:\授权私钥\remote-relay-390529\winremote\relay-security.json
```

The board product UI starts the formal relay and relay input by default. Normal
WinRemote operation expects authenticated `/remote/info` to report
`view_only=false` and `input_enabled=true`; marker-file and plaintext
compatibility paths are retired.

Then connect the Windows client:

```powershell
.\publish\win-x64\8ax.WinRemote.exe --relay-security-profile D:\授权私钥\remote-relay-390529\winremote\relay-security.json
```

In relay input mode, a normal click sends `down -> up`; holding the left mouse
button and dragging sends throttled `down -> move* -> up` pointer events. This
is the formal path for horizontally dragging wide board pages such as the
settings axis-parameter table.
If relay input is configured but currently blocked, the first click retries the
relay input grant and sends the click after the grant succeeds.

The board relay files are:

```text
/run/8ax_v5_product_ui/remote_framebuffer.bgra
/run/8ax_v5_product_ui/remote_dirty
/run/8ax_v5_product_ui/remote_input
/run/8ax_v5_product_ui/ui_ready.json
```

## Release Build

```powershell
.\tools\publish_winremote_update.ps1 -Version YYYY.MMDD.HHMM -ReleaseSequence <monotonic-integer>
.\publish\win-x64\8ax.WinRemote.exe --relay-security-profile D:\授权私钥\remote-relay-390529\winremote\relay-security.json
```

Do not hand off a newly generated WinRemote release package unless the publish
script completed the VPS upload, detached-signature verification, raw three-file
readback, and both primary/backup HTTPS release-set checks in the same run.

## Next Steps

Follow `娴溿倖甯?md` and the active v5 work note:

1. Keep the TLS/challenge/scope negative tests and the real-board HTTPS/WSS acceptance current.
2. Keep closing relay input fixes with true Win mouse input, board `pointer_ack` under 50ms, and first Win-side button feedback under 200ms.
