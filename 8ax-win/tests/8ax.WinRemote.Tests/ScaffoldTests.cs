using System.Collections;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Windows;
using EightAxis.WinRemote;
using EightAxis.WinRemote.Config;
using EightAxis.WinRemote.Diagnostics;
using EightAxis.WinRemote.Input;
using EightAxis.WinRemote.Protocol;
using EightAxis.WinRemote.Rendering;
using EightAxis.WinRemote.Transport;
using EightAxis.WinRemote.Update;

Require("8ax.WinRemote", typeof(App).Assembly.GetName().Name, "assembly name");
Require("EightAxis.WinRemote", typeof(MainWindow).Namespace, "main window namespace");

RemotePoint center = PointerMapper.Map(new Point(512, 300), new Size(1024, 600));
Require(true, center.IsInside, "center inside");
Require(512, center.X, "center x");
Require(300, center.Y, "center y");

RemotePoint outside = PointerMapper.Map(new Point(-1, 300), new Size(1024, 600));
Require(false, outside.IsInside, "outside left");

VerifyProtocolEnvelopeRoundTrip();
VerifyRemoteInfoSystemMetricsSerialization();
VerifyPointerEventDtoSerialization();
VerifyFullFrameAndDirtyRectApply();
VerifyBaseFrameMismatchNeedsFullFrame();
VerifyStaleDirtyFrameIsIgnored();
VerifyInvalidPayloadIsRejected();
VerifyOutOfBoundsRectIsRejectedAtomically();
VerifyRgb565Conversion();
VerifyRuntimeEvidenceRecorder();
VerifyAppSettingsDefaults();
VerifyAppSettingsConfigFile();
VerifyUpdateDefaults();
VerifyRelayReconnectContract();
VerifyRelayStreamFailureNoFallbackContract();
VerifyRelayDisplayRefreshRateContract();
VerifyRelayInputRetryContract();
VerifyRelayInputStaleSocketRecoveryContract();
VerifySystemMetricsTopBarContract();
VerifyWinRemoteBoardTimeSyncContract();
VerifyUpgradeProgressContract();
VerifyDpiScalingContract();
VerifyOperatorButtonsContract();
VerifyDiagnosticsDisplaySanitization();
VerifySingleInstanceContract();
VerifySingleExePublishContract();
await VerifyMockRelayReadOnlyClientAsync();

Console.WriteLine("Relay protocol, evidence, mock relay, and read-only client checks passed.");

static void Require<T>(T expected, T actual, string label)
{
    if (!EqualityComparer<T>.Default.Equals(expected, actual))
    {
        throw new InvalidOperationException($"{label}: expected '{expected}', got '{actual}'");
    }
}

static void VerifyProtocolEnvelopeRoundTrip()
{
    FrameMetadata metadata = FrameMetadata.FullFrame(7, PointerMapper.RemoteWidth, PointerMapper.RemoteHeight, PointerMapper.RemoteWidth * 4, RemotePixelFormats.Bgra32);
    byte[] payload = new byte[] { 1, 2, 3, 4 };
    RemoteFramePacket decoded = RemoteProtocolJson.DecodeFrameEnvelope(RemoteProtocolJson.EncodeFrameEnvelope(metadata, payload));
    Require(7L, decoded.Metadata.FrameId, "envelope frame id");
    Require(payload.Length, decoded.PixelPayload.Length, "envelope payload length");
    Require(payload[2], decoded.PixelPayload[2], "envelope payload byte");
}

static void VerifyRemoteInfoSystemMetricsSerialization()
{
    RemoteInfoMessage info = new(
        "8ax-remote-ui/1",
        PointerMapper.RemoteWidth,
        PointerMapper.RemoteHeight,
        RemotePixelFormats.Bgra32,
        PointerMapper.RemoteWidth * 4,
        false,
        new RemoteSystemMetrics(10.0, 11.0, 42.5, 64.0, 420, 1000, 64, 100));
    string json = RemoteProtocolJson.Serialize(info);
    Require(true, json.Contains("\"system_metrics\"", StringComparison.Ordinal), "info system metrics serialized");
    Require(true, json.Contains("\"cpu0_percent\":10", StringComparison.Ordinal), "info cpu0 metric serialized");
    RemoteInfoMessage decoded = RemoteProtocolJson.Deserialize<RemoteInfoMessage>(json);
    Require(10.0, decoded.SystemMetrics?.Cpu0Percent ?? -1.0, "decoded cpu0 percent");
    Require(11.0, decoded.SystemMetrics?.Cpu1Percent ?? -1.0, "decoded cpu1 percent");
    Require(42.5, decoded.SystemMetrics?.MemoryPercent ?? -1.0, "decoded memory percent");
    Require(64.0, decoded.SystemMetrics?.DiskPercent ?? -1.0, "decoded disk percent");
}

static void VerifyPointerEventDtoSerialization()
{
    PointerEventMessage pointer = new(RemoteMessageTypes.PointerEvent, "session-a", "8ax-win", 9, "down", 512, 300, "left", 1234);
    string json = RemoteProtocolJson.Serialize(pointer);
    PointerEventMessage decoded = RemoteProtocolJson.Deserialize<PointerEventMessage>(json);
    Require(RemoteMessageTypes.PointerEvent, decoded.Type, "pointer type");
    Require("down", decoded.Phase, "pointer phase");
    Require(512, decoded.X, "pointer x");
    Require(300, decoded.Y, "pointer y");

    PointerEventMessage move = new(RemoteMessageTypes.PointerEvent, "session-a", "8ax-win", 10, "move", 256, 300, "left", 1235);
    PointerEventMessage decodedMove = RemoteProtocolJson.Deserialize<PointerEventMessage>(RemoteProtocolJson.Serialize(move));
    Require("move", decodedMove.Phase, "pointer move phase");
    Require(256, decodedMove.X, "pointer move x");
    Require(300, decodedMove.Y, "pointer move y");

    ControlRequestMessage request = new(RemoteMessageTypes.ControlRequest, "session-a", "8ax-win", 1235);
    ControlRequestMessage decodedRequest = RemoteProtocolJson.Deserialize<ControlRequestMessage>(RemoteProtocolJson.Serialize(request));
    Require(RemoteMessageTypes.ControlRequest, decodedRequest.Type, "control request type");

    PointerAckMessage ack = new(RemoteMessageTypes.PointerAck, "session-a", 9, "down", true, 1236, null);
    PointerAckMessage decodedAck = RemoteProtocolJson.Deserialize<PointerAckMessage>(RemoteProtocolJson.Serialize(ack));
    Require(RemoteMessageTypes.PointerAck, decodedAck.Type, "pointer ack type");
    Require(9L, decodedAck.Sequence, "pointer ack seq");
    Require(true, decodedAck.Accepted, "pointer ack accepted");
}

static void VerifyFullFrameAndDirtyRectApply()
{
    RemoteFramebuffer framebuffer = new();
    RemoteFrameAssembler assembler = new(framebuffer);

    RemoteFrameApplyResult full = assembler.Apply(new RemoteFramePacket(
        FrameMetadata.FullFrame(10, PointerMapper.RemoteWidth, PointerMapper.RemoteHeight, PointerMapper.RemoteWidth * 4, RemotePixelFormats.Bgra32),
        FullBgraFrame(12, 18, 24)));
    Require(RemoteFrameApplyStatus.AppliedFullFrame, full.Status, "full frame status");
    Require(10L, framebuffer.FrameId, "full frame id");

    byte[] dirtyPayload = SolidBgraRect(2, 2, 80, 90, 100);
    RemoteFrameApplyResult dirty = assembler.Apply(new RemoteFramePacket(
        FrameMetadata.DirtyRects(
            11,
            10,
            PointerMapper.RemoteWidth,
            PointerMapper.RemoteHeight,
            PointerMapper.RemoteWidth * 4,
            RemotePixelFormats.Bgra32,
            new[] { new DirtyRectMetadata(5, 6, 2, 2, "raw") }),
        dirtyPayload));

    Require(RemoteFrameApplyStatus.AppliedDirtyRects, dirty.Status, "dirty status");
    Require(11L, framebuffer.FrameId, "dirty frame id");
    byte[] pixels = framebuffer.CopyBgra32Pixels();
    int offset = PixelOffset(5, 6);
    Require((byte)80, pixels[offset], "dirty blue");
    Require((byte)90, pixels[offset + 1], "dirty green");
    Require((byte)100, pixels[offset + 2], "dirty red");
}

static void VerifyBaseFrameMismatchNeedsFullFrame()
{
    RemoteFramebuffer framebuffer = new();
    RemoteFrameAssembler assembler = new(framebuffer);
    assembler.Apply(new RemoteFramePacket(
        FrameMetadata.FullFrame(1, PointerMapper.RemoteWidth, PointerMapper.RemoteHeight, PointerMapper.RemoteWidth * 4, RemotePixelFormats.Bgra32),
        FullBgraFrame(1, 2, 3)));
    byte[] before = framebuffer.CopyBgra32Pixels();

    RemoteFrameApplyResult result = assembler.Apply(new RemoteFramePacket(
        FrameMetadata.DirtyRects(
            3,
            2,
            PointerMapper.RemoteWidth,
            PointerMapper.RemoteHeight,
            PointerMapper.RemoteWidth * 4,
            RemotePixelFormats.Bgra32,
            new[] { new DirtyRectMetadata(0, 0, 1, 1, "raw") }),
        SolidBgraRect(1, 1, 200, 210, 220)));

    Require(RemoteFrameApplyStatus.NeedFullFrame, result.Status, "base mismatch status");
    Require(1L, framebuffer.FrameId, "base mismatch keeps frame id");
    Require(before[0], framebuffer.CopyBgra32Pixels()[0], "base mismatch keeps pixels");
}

static void VerifyStaleDirtyFrameIsIgnored()
{
    RemoteFramebuffer framebuffer = new();
    RemoteFrameAssembler assembler = new(framebuffer);
    assembler.Apply(new RemoteFramePacket(
        FrameMetadata.FullFrame(10, PointerMapper.RemoteWidth, PointerMapper.RemoteHeight, PointerMapper.RemoteWidth * 4, RemotePixelFormats.Bgra32),
        FullBgraFrame(1, 2, 3)));
    byte[] before = framebuffer.CopyBgra32Pixels();

    RemoteFrameApplyResult result = assembler.Apply(new RemoteFramePacket(
        FrameMetadata.DirtyRects(
            9,
            8,
            PointerMapper.RemoteWidth,
            PointerMapper.RemoteHeight,
            PointerMapper.RemoteWidth * 4,
            RemotePixelFormats.Bgra32,
            new[] { new DirtyRectMetadata(0, 0, 1, 1, "raw") }),
        SolidBgraRect(1, 1, 200, 210, 220)));

    Require(RemoteFrameApplyStatus.StaleFrame, result.Status, "stale dirty status");
    Require(10L, framebuffer.FrameId, "stale dirty keeps frame id");
    Require(before[0], framebuffer.CopyBgra32Pixels()[0], "stale dirty keeps pixels");
}

static void VerifyInvalidPayloadIsRejected()
{
    RemoteFramebuffer framebuffer = new();
    RemoteFrameAssembler assembler = new(framebuffer);
    RemoteFrameApplyResult result = assembler.Apply(new RemoteFramePacket(
        FrameMetadata.FullFrame(1, PointerMapper.RemoteWidth, PointerMapper.RemoteHeight, PointerMapper.RemoteWidth * 4, RemotePixelFormats.Bgra32),
        new byte[16]));

    Require(RemoteFrameApplyStatus.Rejected, result.Status, "short payload status");
    Require(0L, framebuffer.FrameId, "short payload frame id");
}

static void VerifyOutOfBoundsRectIsRejectedAtomically()
{
    RemoteFramebuffer framebuffer = new();
    RemoteFrameAssembler assembler = new(framebuffer);
    assembler.Apply(new RemoteFramePacket(
        FrameMetadata.FullFrame(1, PointerMapper.RemoteWidth, PointerMapper.RemoteHeight, PointerMapper.RemoteWidth * 4, RemotePixelFormats.Bgra32),
        FullBgraFrame(9, 10, 11)));
    byte[] before = framebuffer.CopyBgra32Pixels();

    RemoteFrameApplyResult result = assembler.Apply(new RemoteFramePacket(
        FrameMetadata.DirtyRects(
            2,
            1,
            PointerMapper.RemoteWidth,
            PointerMapper.RemoteHeight,
            PointerMapper.RemoteWidth * 4,
            RemotePixelFormats.Bgra32,
            new[] { new DirtyRectMetadata(PointerMapper.RemoteWidth - 1, 0, 2, 1, "raw") }),
        SolidBgraRect(2, 1, 1, 2, 3)));

    Require(RemoteFrameApplyStatus.Rejected, result.Status, "out-of-bounds status");
    Require(1L, framebuffer.FrameId, "out-of-bounds keeps frame id");
    Require(before[0], framebuffer.CopyBgra32Pixels()[0], "out-of-bounds keeps pixels");
}

static void VerifyRgb565Conversion()
{
    byte[] rgb565Red = new byte[] { 0x00, 0xF8 };
    byte[] bgra = PixelFormatConverter.ConvertPackedRectToBgra32(RemotePixelFormats.Rgb565, 1, 1, rgb565Red);
    Require((byte)0, bgra[0], "rgb565 red blue");
    Require((byte)0, bgra[1], "rgb565 red green");
    Require((byte)255, bgra[2], "rgb565 red red");
    Require((byte)255, bgra[3], "rgb565 red alpha");
}

static void VerifyRuntimeEvidenceRecorder()
{
    string evidenceDir = Path.Combine(Path.GetTempPath(), "8ax-win-tests", Guid.NewGuid().ToString("N"));
    RuntimeEvidenceRecorder recorder = new(evidenceDir);
    FrameStats stats = new();
    stats.MarkFullFrameRequest();
    stats.MarkDirtyRectFrame();
    stats.MarkFullFrameRepair();
    stats.MarkRejectedFrame();
    stats.MarkFrame(12, 34);
    recorder.RecordEvent("token_redaction_check", new Dictionary<string, object?>
    {
        ["session_token"] = "should-not-appear",
        ["message"] = "ok",
    });
    recorder.RecordMetrics(stats, "unit-test");

    Require(true, File.Exists(recorder.EventsPath), "events jsonl exists");
    Require(true, File.Exists(recorder.MetricsPath), "metrics jsonl exists");
    string events = File.ReadAllText(recorder.EventsPath);
    string metrics = File.ReadAllText(recorder.MetricsPath);
    Require(true, events.Contains("[redacted]", StringComparison.Ordinal), "events token redacted");
    Require(false, events.Contains("should-not-appear", StringComparison.Ordinal), "events no raw token");
    Require(true, metrics.Contains("\"full_frame_requests\":1", StringComparison.Ordinal), "metrics full-frame count");
    Require(true, metrics.Contains("\"dirty_rect_frames\":1", StringComparison.Ordinal), "metrics dirty count");
}

static void VerifyAppSettingsDefaults()
{
    string configDir = Path.Combine(Path.GetTempPath(), "8ax-win-tests", Guid.NewGuid().ToString("N"));
    Directory.CreateDirectory(configDir);

    AppSettings defaultSettings = AppSettings.Load(
        new[] { "8ax.WinRemote.exe" },
        new Hashtable(),
        configDir);
    Require(RemoteSourceMode.Relay, defaultSettings.SourceMode, "default source mode");
    Require(AppSettings.DefaultRelayUrl, defaultSettings.RelayBaseUri?.ToString(), "default relay uri");
    Require(false, defaultSettings.ViewOnly, "default relay view-only");
    Require(false, defaultSettings.EnablePointer, "default relay temp pointer disabled");
    Require(true, defaultSettings.EnableRemoteInput, "default relay input enabled");

    AppSettings viewOnlySettings = AppSettings.Load(
        new[] { "8ax.WinRemote.exe", "--view-only", "true" },
        new Hashtable(),
        configDir);
    Require(RemoteSourceMode.Relay, viewOnlySettings.SourceMode, "view-only source mode");
    Require(true, viewOnlySettings.ViewOnly, "view-only relay flag");
    Require(false, viewOnlySettings.EnableRemoteInput, "view-only disables relay input by default");

    AppSettings retiredBoardFlagSettings = AppSettings.Load(
        new[] { "8ax.WinRemote.exe", "--board-fb0", "true", "--enable-pointer", "true" },
        new Hashtable(),
        configDir);
    Require(RemoteSourceMode.Relay, retiredBoardFlagSettings.SourceMode, "retired board-fb0 source mode");
    Require(AppSettings.DefaultRelayUrl, retiredBoardFlagSettings.RelayBaseUri?.ToString(), "retired board-fb0 relay uri");
    Require(false, retiredBoardFlagSettings.ViewOnly, "retired board-fb0 view-only");
    Require(true, retiredBoardFlagSettings.EnablePointer, "retired board-fb0 pointer flag is ignored only by relay policy");
    Require(true, retiredBoardFlagSettings.EnableRemoteInput, "retired board-fb0 relay input remains default");
}

static void VerifyAppSettingsConfigFile()
{
    string configDir = Path.Combine(Path.GetTempPath(), "8ax-win-tests", Guid.NewGuid().ToString("N"));
    Directory.CreateDirectory(configDir);
    string configPath = Path.Combine(configDir, "client.json");
    string evidenceDir = Path.Combine(configDir, "evidence");
    File.WriteAllText(
        configPath,
        $$"""
        {
          "relay_url": "http://127.0.0.1:18090/",
          "evidence_dir": "{{evidenceDir.Replace("\\", "\\\\")}}",
          "view_only": false,
          "enable_pointer": true,
          "enable_remote_input": true
        }
        """);

    AppSettings configSettings = AppSettings.Load(
        new[] { "8ax.WinRemote.exe", "--config", configPath },
        new Hashtable(),
        configDir);
    Require(RemoteSourceMode.Relay, configSettings.SourceMode, "config source mode");
    Require("http://127.0.0.1:18090/", configSettings.RelayBaseUri?.ToString(), "config relay uri");
    Require(evidenceDir, configSettings.EvidenceDirectory, "config evidence dir");
    Require(false, configSettings.ViewOnly, "config view-only");
    Require(true, configSettings.EnablePointer, "config enable pointer");
    Require(true, configSettings.EnableRemoteInput, "config enable remote input");

    AppSettings overrideSettings = AppSettings.Load(
        new[] { "8ax.WinRemote.exe", "--config", configPath, "--relay", "http://127.0.0.1:18091/", "--view-only", "true", "--enable-pointer", "false", "--enable-remote-input", "false" },
        new Hashtable(),
        configDir);
    Require("http://127.0.0.1:18091/", overrideSettings.RelayBaseUri?.ToString(), "cli relay override");
    Require(true, overrideSettings.ViewOnly, "cli view-only override");
    Require(false, overrideSettings.EnablePointer, "cli enable pointer override");
    Require(false, overrideSettings.EnableRemoteInput, "cli enable remote input override");

    AppSettings retiredBoardOverrideSettings = AppSettings.Load(
        new[] { "8ax.WinRemote.exe", "--config", configPath, "--board-fb0", "true", "--enable-pointer", "true" },
        new Hashtable(),
        configDir);
    Require(RemoteSourceMode.Relay, retiredBoardOverrideSettings.SourceMode, "cli board-fb0 retired override");
    Require("http://127.0.0.1:18090/", retiredBoardOverrideSettings.RelayBaseUri?.ToString(), "cli board-fb0 uses config relay uri");
    Require(true, retiredBoardOverrideSettings.EnablePointer, "cli board-fb0 no longer disables relay pointer flag");
    Require(true, retiredBoardOverrideSettings.EnableRemoteInput, "cli board-fb0 keeps relay input");
}

static void VerifyUpdateDefaults()
{
    Require("8ax.WinRemote", WinRemoteUpdater.AppId, "update app id");
    Require("8ax.WinRemote.exe", WinRemoteUpdater.ExecutableName, "fixed update exe name");
    Require("https://license.cjwsjzyy.xyz/8ax-winremote/win-x64/manifest.json", WinRemoteUpdater.DefaultManifestUris[0].ToString(), "primary update manifest");
    Require("https://license.3dtouch.top/8ax-winremote/win-x64/manifest.json", WinRemoteUpdater.DefaultManifestUris[1].ToString(), "backup update manifest");
    Require(1, WinRemoteUpdater.CompareVersions("2026.0618.2010", "2026.0618.2008"), "remote newer version comparison");
    Require(0, WinRemoteUpdater.CompareVersions("2026.0618.2008", "2026.0618.2008"), "same version comparison");
    Require(-1, WinRemoteUpdater.CompareVersions("2026.0618.2008", "2026.0618.2010"), "remote older version comparison");
    Require(true, !String.IsNullOrWhiteSpace(WinRemoteUpdater.CurrentVersion()), "local current version readable");
}

static void VerifyRelayReconnectContract()
{
    string repoRoot = FindRepoRoot(AppContext.BaseDirectory);
    string source = File.ReadAllText(Path.Combine(repoRoot, "8ax-win", "src", "8ax.WinRemote", "MainWindow.xaml.cs"));
    string readme = File.ReadAllText(Path.Combine(repoRoot, "8ax-win", "README.md"));
    Require(true, source.Contains("RunRelayLoopAsync", StringComparison.Ordinal), "relay reconnect loop exists");
    Require(true, source.Contains("relay_reconnect_scheduled", StringComparison.Ordinal), "relay reconnect evidence exists");
    Require(true, source.Contains("RelayReconnectMaxDelayMs", StringComparison.Ordinal), "relay reconnect backoff cap exists");
    Require(true, source.Contains("MarkRelayDisconnected", StringComparison.Ordinal), "relay disconnect state reset exists");
    Require(true, source.Contains("_relaySessionConnected", StringComparison.Ordinal), "relay connected-session guard exists");
    Require(true, source.Contains("reconnectAttempt > 0 && !_relaySessionConnected", StringComparison.Ordinal), "relay reconnect UI only shows after real disconnect");
    Require(true, source.Contains("RunRelaySessionAsync(relayBaseUri, relayClient, reconnectAttempt > 0)", StringComparison.Ordinal), "relay reconnect attempts do not reset badge to connecting");
    Require(false, source.Contains("SetConnectionState(\"reconnecting\"", StringComparison.Ordinal), "top-right badge must not jump to reconnecting");
    Require(true, source.Contains("relay: reconnecting attempt", StringComparison.Ordinal), "reconnect attempt stays in status text");
    Require(true, source.Contains("connection_state_changed", StringComparison.Ordinal), "connection badge state changes are evidenced");
    Require(true, source.Contains("connectedBeforeEnd = await RunRelaySessionAsync", StringComparison.Ordinal), "relay reconnect detects connected sessions");
    Require(true, source.Contains("reconnectAttempt = 0", StringComparison.Ordinal), "relay reconnect resets attempt after connected session");
    Require(true, source.Contains("private async Task<bool> RunRelaySessionAsync(Uri relayBaseUri, RemoteRelayClient relayClient, bool isReconnectAttempt)", StringComparison.Ordinal), "relay session reports connected-before-end state");
    Require(true,
        readme.Contains("stable `error`", StringComparison.Ordinal)
            && readme.Contains("retry attempts are shown only in the bottom status text and evidence", StringComparison.Ordinal),
        "README documents stable top-right error badge");
}

static void VerifyRelayStreamFailureNoFallbackContract()
{
    string repoRoot = FindRepoRoot(AppContext.BaseDirectory);
    string mainWindow = File.ReadAllText(Path.Combine(repoRoot, "8ax-win", "src", "8ax.WinRemote", "MainWindow.xaml.cs"));
    string relayClient = File.ReadAllText(Path.Combine(repoRoot, "8ax-win", "src", "8ax.WinRemote", "Transport", "RemoteRelayClient.cs"));
    string readme = File.ReadAllText(Path.Combine(repoRoot, "8ax-win", "README.md"));
    Require(false, mainWindow.Contains("RunFullFramePollingAsync", StringComparison.Ordinal), "relay HTTP polling fallback is retired");
    Require(true, mainWindow.Contains("relay_stream_unavailable", StringComparison.Ordinal), "relay stream failure evidence exists");
    Require(true, mainWindow.Contains("[\"action\"] = \"reconnect\"", StringComparison.Ordinal), "relay stream failure schedules reconnect");
    Require(false, mainWindow.Contains("\"polling_fallback\"", StringComparison.Ordinal), "relay polling fallback reason is retired");
    Require(true, mainWindow.Contains("ApplyRelayPacket(packet, relayBaseUri, \"stream-retry\")", StringComparison.Ordinal), "dirty frame is replayed after full repair");
    Require(true, mainWindow.Contains("frame_stale", StringComparison.Ordinal), "stale dirty frames are ignored without repair loop");
    Require(false, mainWindow.Contains("SetConnectionState(\"polling\"", StringComparison.Ordinal), "relay polling badge is retired");
    Require(false, mainWindow.Contains("SetConnectionState(\"recovering\"", StringComparison.Ordinal), "full-frame repair must not make top-right badge jump");
    Require(true, relayClient.Contains("StreamConnectTimeout", StringComparison.Ordinal), "relay stream connect timeout exists");
    Require(true, relayClient.Contains("InputConnectTimeout", StringComparison.Ordinal), "relay input connect timeout exists");
    Require(true, relayClient.Contains("WebSocket connect timed out", StringComparison.Ordinal), "websocket timeout is explicit");
    Require(true, readme.Contains("does not fall back to relay polling", StringComparison.Ordinal), "README documents relay polling retirement");
    Require(false, mainWindow.Contains("/dev/fb0", StringComparison.Ordinal), "fallback does not use retired fb0 path");
}

static void VerifyRelayDisplayRefreshRateContract()
{
    string winRoot = FindWinRemoteRoot(AppContext.BaseDirectory);
    string mainWindow = ReadWinRemoteFile(winRoot, "src", "8ax.WinRemote", "MainWindow.xaml.cs");
    string mockRelay = ReadWinRemoteFile(winRoot, "tools", "mock-relay", "Program.cs");
    string readme = ReadWinRemoteFile(winRoot, "README.md");
    Require(true, mainWindow.Contains("RelayStreamTargetFps = 30", StringComparison.Ordinal), "relay target fps is 30");
    Require(true, mainWindow.Contains("RelayFrameMetricsMinIntervalMs = 1000.0 / RelayStreamTargetFps", StringComparison.Ordinal), "relay frame metrics cadence derives from target fps");
    Require(true, mainWindow.Contains("ShouldRecordRelayFrameMetrics", StringComparison.Ordinal), "relay status and metrics are throttled to target cadence");
    Require(true, mainWindow.Contains("[\"target_fps\"] = RelayStreamTargetFps", StringComparison.Ordinal), "relay target fps is recorded in session evidence");
    Require(true, mockRelay.Contains("StreamTargetFps = 30", StringComparison.Ordinal), "mock relay target fps is 30");
    Require(true, mockRelay.Contains("StreamFrameIntervalTicks = (long)Math.Round(Stopwatch.Frequency / (double)StreamTargetFps)", StringComparison.Ordinal), "mock relay frame interval derives from target fps");
    Require(true, mockRelay.Contains("PaceToNextStreamFrameAsync", StringComparison.Ordinal), "mock relay uses target-time stream pacing");
    Require(true,
        readme.Contains("targets 30Hz", StringComparison.Ordinal)
            && readme.Contains("WebSocket dirty-rect stream", StringComparison.Ordinal)
            && readme.Contains("must not switch to HTTP full-frame polling", StringComparison.Ordinal),
        "README documents 30Hz dirty-rect relay stream without full-frame polling");
}

static void VerifyRelayInputRetryContract()
{
    string repoRoot = FindRepoRoot(AppContext.BaseDirectory);
    string source = File.ReadAllText(Path.Combine(repoRoot, "8ax-win", "src", "8ax.WinRemote", "MainWindow.xaml.cs"));
    Require(true, source.Contains("EnsureRelayInputReadyAsync", StringComparison.Ordinal), "relay input retry helper exists");
    Require(true, source.Contains("relay_input_retry_started", StringComparison.Ordinal), "relay input retry evidence starts");
    Require(true, source.Contains("relay_input_retry_ready", StringComparison.Ordinal), "relay input retry ready evidence exists");
    Require(true, source.Contains("bool enabled = await EnsureRelayInputReadyAsync()", StringComparison.Ordinal), "mouse down retries relay input before dropping click");
    Require(true, source.Contains("_relayInputEnsureActive", StringComparison.Ordinal), "relay input retry is gated");
}

static void VerifyRelayInputStaleSocketRecoveryContract()
{
    string winRoot = FindWinRemoteRoot(AppContext.BaseDirectory);
    string mainWindow = ReadWinRemoteFile(winRoot, "src", "8ax.WinRemote", "MainWindow.xaml.cs");
    string relayClient = ReadWinRemoteFile(winRoot, "src", "8ax.WinRemote", "Transport", "RemoteRelayClient.cs");
    Require(true, relayClient.Contains("ResetInputSocketLocked", StringComparison.Ordinal), "stale relay input socket is reset after pointer failure");
    Require(true, relayClient.Contains("catch", StringComparison.Ordinal) && relayClient.Contains("ResetInputSocketLocked();", StringComparison.Ordinal), "pointer send failure discards cached input socket");
    Require(true, mainWindow.Contains("relay_pointer_reconnect_retry_started", StringComparison.Ordinal), "mouse down records reconnect retry evidence");
    Require(true, mainWindow.Contains("allowReconnectRetry: false", StringComparison.Ordinal), "mouse down reconnect retry is bounded to one retry");
    Require(true, mainWindow.Contains("_relayInputReady = false", StringComparison.Ordinal), "pointer failure clears relay input ready state");
    Require(true, mainWindow.Contains("if (await EnsureRelayInputReadyAsync())", StringComparison.Ordinal), "mouse down retry reacquires relay input grant");
}

static void VerifySystemMetricsTopBarContract()
{
    string winRoot = FindWinRemoteRoot(AppContext.BaseDirectory);
    string xaml = ReadWinRemoteFile(winRoot, "src", "8ax.WinRemote", "MainWindow.xaml");
    string mainWindow = ReadWinRemoteFile(winRoot, "src", "8ax.WinRemote", "MainWindow.xaml.cs");
    string protocol = ReadWinRemoteFile(winRoot, "src", "8ax.WinRemote", "Protocol", "RemoteMessage.cs");
    string readme = ReadWinRemoteFile(winRoot, "README.md");
    Require(true, xaml.Contains("SystemMetricsText", StringComparison.Ordinal), "top bar system metrics text exists");
    Require(true, xaml.Contains("<Run Text=\"cpu0 \"", StringComparison.Ordinal), "top bar cpu0 label is static");
    Require(true, xaml.Contains("x:Name=\"Cpu0MetricValue\"", StringComparison.Ordinal), "top bar cpu0 value field exists");
    Require(true, xaml.Contains("x:Name=\"Cpu1MetricValue\"", StringComparison.Ordinal), "top bar cpu1 value field exists");
    Require(true, xaml.Contains("x:Name=\"MemoryMetricValue\"", StringComparison.Ordinal), "top bar memory value field exists");
    Require(true, xaml.Contains("x:Name=\"DiskMetricValue\"", StringComparison.Ordinal), "top bar disk value field exists");
    Require(true, mainWindow.Contains("RefreshSystemMetricsAsync", StringComparison.Ordinal), "win remote refreshes system metrics");
    Require(true, mainWindow.Contains("FormatSystemMetrics", StringComparison.Ordinal), "win remote formats system metrics");
    Require(true, mainWindow.Contains("Cpu0MetricValue.Text = Percent(metrics?.Cpu0Percent)", StringComparison.Ordinal), "cpu0 refresh updates only the value field");
    Require(true, mainWindow.Contains("Cpu1MetricValue.Text = Percent(metrics?.Cpu1Percent)", StringComparison.Ordinal), "cpu1 refresh updates only the value field");
    Require(true, mainWindow.Contains("MemoryMetricValue.Text = Percent(metrics?.MemoryPercent)", StringComparison.Ordinal), "memory refresh updates only the value field");
    Require(true, mainWindow.Contains("DiskMetricValue.Text = Percent(metrics?.DiskPercent)", StringComparison.Ordinal), "disk refresh updates only the value field");
    Require(false, mainWindow.Contains("SystemMetricsText.Text =", StringComparison.Ordinal), "system metrics refresh must not rewrite the whole line");
    Require(true, protocol.Contains("RemoteSystemMetrics", StringComparison.Ordinal), "system metrics DTO exists");
    Require(true, readme.Contains("Top-bar board resource diagnostics", StringComparison.Ordinal), "README documents resource diagnostics");
    Require(true,
        readme.Contains("refreshes may update only the four percentage", StringComparison.Ordinal)
            && readme.Contains("number fields", StringComparison.Ordinal),
        "README documents number-only refresh");
}

static void VerifyWinRemoteBoardTimeSyncContract()
{
    string winRoot = FindWinRemoteRoot(AppContext.BaseDirectory);
    string relayClient = ReadWinRemoteFile(winRoot, "src", "8ax.WinRemote", "Transport", "RemoteRelayClient.cs");
    string winReadme = ReadWinRemoteFile(winRoot, "README.md");
    Require(true, relayClient.Contains("X-8ax-Client-Time-Unix-Ms", StringComparison.Ordinal), "WinRemote sends client time unix ms header");
    Require(true, relayClient.Contains("X-8ax-Client-Time-Source", StringComparison.Ordinal), "WinRemote sends client time source header");
    Require(true, relayClient.Contains("NetworkTimeUris", StringComparison.Ordinal), "WinRemote has network time probe list");
    Require(true, relayClient.Contains("winremote-network-time", StringComparison.Ordinal), "WinRemote reports network time source");
    Require(true, relayClient.Contains("winremote-local-time", StringComparison.Ordinal), "WinRemote reports local fallback time source");
    Require(true, relayClient.Contains("DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()", StringComparison.Ordinal), "WinRemote falls back to local UTC clock");
    Require(true, winReadme.Contains("falls back to the Windows local UTC clock", StringComparison.Ordinal), "README documents local fallback");
    Require(true,
        winReadme.Contains("may set the system", StringComparison.Ordinal)
            && winReadme.Contains("clock once", StringComparison.Ordinal),
        "README documents one-shot board time set");
}

static void VerifyUpgradeProgressContract()
{
    string repoRoot = FindRepoRoot(AppContext.BaseDirectory);
    string mainWindow = File.ReadAllText(Path.Combine(repoRoot, "8ax-win", "src", "8ax.WinRemote", "MainWindow.xaml.cs"));
    string updater = File.ReadAllText(Path.Combine(repoRoot, "8ax-win", "src", "8ax.WinRemote", "Update", "WinRemoteUpdater.cs"));
    string dialog = File.ReadAllText(Path.Combine(repoRoot, "8ax-win", "src", "8ax.WinRemote", "Update", "UpdateProgressDialog.cs"));
    Require(true, mainWindow.Contains("UpdateProgressDialog", StringComparison.Ordinal), "upgrade opens progress dialog");
    Require(true, mainWindow.Contains("progressDialog.Show()", StringComparison.Ordinal), "upgrade progress dialog shown");
    Require(true, mainWindow.Contains("progressDialog.Activate()", StringComparison.Ordinal), "upgrade progress dialog activated");
    Require(true, mainWindow.Contains("CheckForVpsUpdateAsync", StringComparison.Ordinal), "main window checks VPS manifest on startup");
    Require(true, mainWindow.Contains("UpgradeButton.Visibility = _updateAvailable ? Visibility.Visible : Visibility.Collapsed", StringComparison.Ordinal), "upgrade button is hidden unless an update is available");
    Require(true, mainWindow.Contains("UpgradeButton.Content = \"\\u5347\\u7ea7\"", StringComparison.Ordinal), "available update button shows upgrade action");
    Require(true, mainWindow.Contains("update_available", StringComparison.Ordinal), "available update evidence is recorded");
    Require(true, updater.Contains("IProgress<UpdateProgress>", StringComparison.Ordinal), "updater exposes progress");
    Require(true, updater.Contains("Task<UpdateCheckResult> CheckAsync", StringComparison.Ordinal), "updater exposes read-only update check");
    Require(true, updater.Contains("UpdateNotNeededException", StringComparison.Ordinal), "updater has no-update result");
    Require(true, updater.Contains("CompareVersions(manifest.Version, localVersion)", StringComparison.Ordinal), "updater compares remote and local version before download");
    Require(true, mainWindow.Contains("catch (UpdateNotNeededException", StringComparison.Ordinal), "main window handles already-current update");
    Require(true, updater.Contains("ResponseHeadersRead", StringComparison.Ordinal), "download streams with progress");
    Require(true, updater.Contains("ManifestRequestTimeout", StringComparison.Ordinal), "manifest read has timeout");
    Require(true, updater.Contains("PackageDownloadTimeout", StringComparison.Ordinal), "package download has timeout");
    Require(true, updater.Contains("TrimStart('\\uFEFF')", StringComparison.Ordinal), "manifest BOM is stripped");
    Require(true, updater.Contains("ManifestSourceName", StringComparison.Ordinal), "manifest progress names primary source");
    Require(true, updater.Contains("DefaultManifestUris", StringComparison.Ordinal), "manifest progress names backup source");
    Require(false, updater.Contains("UseProxy = false", StringComparison.Ordinal), "updater uses system network/proxy");
    Require(true, updater.Contains("single-exe update package has unexpected files", StringComparison.Ordinal), "updater rejects multi-file package");
    Require(true, updater.Contains("8ax.WinRemote.dll", StringComparison.Ordinal), "updater removes old sidecar dll");
    Require(true, updater.Contains("8ax.WinRemote.deps.json", StringComparison.Ordinal), "updater removes old deps sidecar");
    Require(true, updater.Contains("install.log", StringComparison.Ordinal), "installer writes install log");
    Require(true, updater.Contains("-ParentPid", StringComparison.Ordinal), "installer avoids read-only PowerShell PID variable");
    Require(true, updater.Contains("restart ok", StringComparison.Ordinal), "installer logs restart success");
    Require(true, updater.Contains("Start-Process -FilePath $targetExe -WorkingDirectory $Target -PassThru", StringComparison.Ordinal), "installer restarts updated exe");
    Require(true, dialog.Contains("ProgressBar", StringComparison.Ordinal), "progress dialog has progress bar");
    Require(true, dialog.Contains("Topmost = true", StringComparison.Ordinal), "progress dialog is topmost");
    Require(true, dialog.Contains("MarkDone", StringComparison.Ordinal), "progress dialog supports already-current state");
    Require(true, dialog.Contains("AlreadyCurrentHint", StringComparison.Ordinal), "already-current state has dedicated hint");
    Require(true, dialog.Contains("FailedHint", StringComparison.Ordinal), "failed state has dedicated hint");
    Require(true, dialog.Contains("_hintText.Text = AlreadyCurrentHint", StringComparison.Ordinal), "already-current dialog does not show download/restart hint");
}

static void VerifyDpiScalingContract()
{
    string repoRoot = FindRepoRoot(AppContext.BaseDirectory);
    string project = File.ReadAllText(Path.Combine(repoRoot, "8ax-win", "src", "8ax.WinRemote", "8ax.WinRemote.csproj"));
    string manifest = File.ReadAllText(Path.Combine(repoRoot, "8ax-win", "src", "8ax.WinRemote", "app.manifest"));
    string dialog = File.ReadAllText(Path.Combine(repoRoot, "8ax-win", "src", "8ax.WinRemote", "Update", "UpdateProgressDialog.cs"));
    Require(true, project.Contains("<ApplicationManifest>app.manifest</ApplicationManifest>", StringComparison.Ordinal), "app manifest configured");
    Require(true, manifest.Contains("PerMonitorV2", StringComparison.Ordinal), "per-monitor v2 dpi awareness");
    Require(true, dialog.Contains("SizeToContent = SizeToContent.Height", StringComparison.Ordinal), "progress dialog height follows content");
    Require(false, dialog.Contains("Height = 210", StringComparison.Ordinal), "progress dialog no fixed clipped height");
    Require(true, dialog.Contains("UseLayoutRounding = true", StringComparison.Ordinal), "progress dialog layout rounding");
}

static void VerifyOperatorButtonsContract()
{
    string winRoot = FindWinRemoteRoot(AppContext.BaseDirectory);
    string xaml = ReadWinRemoteFile(winRoot, "src", "8ax.WinRemote", "MainWindow.xaml");
    string mainWindow = ReadWinRemoteFile(winRoot, "src", "8ax.WinRemote", "MainWindow.xaml.cs");
    string otaWindow = ReadWinRemoteFile(winRoot, "src", "8ax.WinRemote", "MainWindow.OtaUpgrade.cs");
    string relayClient = ReadWinRemoteFile(winRoot, "src", "8ax.WinRemote", "Transport", "RemoteRelayClient.cs");
    string programDialog = ReadWinRemoteFile(winRoot, "src", "8ax.WinRemote", "ProgramDirectoryDialog.cs");
    string logDialog = ReadWinRemoteFile(winRoot, "src", "8ax.WinRemote", "Diagnostics", "ClientLogPreviewDialog.cs");
    string readme = ReadWinRemoteFile(winRoot, "README.md");
    string buttonDoc = readme;
    string mockRelay = ReadWinRemoteFile(winRoot, "tools", "mock-relay", "Program.cs");
    Require(true, xaml.Contains("Height=\"920\"", StringComparison.Ordinal), "main window default height enlarged");
    Require(true, xaml.Contains("Width=\"1440\"", StringComparison.Ordinal), "main window default width enlarged");
    Require(true, xaml.Contains("MinHeight=\"840\"", StringComparison.Ordinal), "main window minimum height enlarged");
    Require(true, xaml.Contains("MinWidth=\"1280\"", StringComparison.Ordinal), "main window minimum width enlarged");
    Require(true, xaml.Contains("x:Name=\"ReadLogButton\"", StringComparison.Ordinal), "read log button exists");
    Require(true, xaml.Contains("x:Name=\"UploadGCodeButton\"", StringComparison.Ordinal), "upload gcode button exists");
    Require(true, xaml.Contains("x:Name=\"OpenSystemGCodeButton\"", StringComparison.Ordinal), "open system gcode button exists");
    Require(true, xaml.Contains("x:Name=\"OtaUpgradeButton\"", StringComparison.Ordinal), "OTA upgrade button exists");
    Require(true, xaml.Contains("Content=\"&#35835;&#21462;&#26085;&#24535;\"", StringComparison.Ordinal), "read log button label");
    Require(true, xaml.Contains("Content=\"&#20256;G&#20195;&#30721;\"", StringComparison.Ordinal), "gcode button label");
    Require(true, xaml.Contains("Content=\"&#25171;&#24320;&#31995;&#32479;G&#20195;&#30721;\"", StringComparison.Ordinal), "open system gcode button label");
    Require(true, xaml.Contains("Content=\"OTA&#21319;&#32423;\"", StringComparison.Ordinal), "OTA upgrade button label");
    Require(true, mainWindow.Contains("ReadLogButton_OnClick", StringComparison.Ordinal), "read log click handler wired");
    Require(true, mainWindow.Contains("FetchBoardDiagnosticsAsync", StringComparison.Ordinal), "read log fetches board diagnostics");
    Require(true, mainWindow.Contains("board_diagnostics_received", StringComparison.Ordinal), "read log records board diagnostics evidence");
    Require(true, relayClient.Contains("remote/diagnostics", StringComparison.Ordinal), "relay client calls board diagnostics endpoint");
    Require(true, logDialog.Contains("Func<Task<string>>", StringComparison.Ordinal), "diagnostics dialog supports refresh from relay");
    Require(true, logDialog.Contains("TextWrapping.Wrap", StringComparison.Ordinal), "diagnostics dialog wraps long lines");
    Require(true, logDialog.Contains("HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled", StringComparison.Ordinal), "diagnostics dialog does not rely on horizontal scrolling");
    Require(true, logDialog.Contains("JsonDocument.Parse", StringComparison.Ordinal), "diagnostics dialog parses JSON before display");
    Require(true, logDialog.Contains("WriteIndented = true", StringComparison.Ordinal), "diagnostics dialog pretty-prints JSON");
    Require(true, logDialog.Contains("SensitiveDiagnosticsTokenPattern", StringComparison.Ordinal), "diagnostics dialog has sensitive token filter");
    Require(true, logDialog.Contains("SanitizeDiagnosticsText", StringComparison.Ordinal), "diagnostics dialog sanitizes displayed diagnostics");
    Require(true, logDialog.Contains("BuildSummaryDisplayText", StringComparison.Ordinal), "diagnostics dialog builds operator summary");
    Require(true, logDialog.Contains("ToggleDetails", StringComparison.Ordinal), "diagnostics dialog can toggle detailed redacted JSON");
    Require(true, mainWindow.Contains("OpenFileDialog", StringComparison.Ordinal), "gcode button opens local file picker");
    Require(true, mainWindow.Contains("UploadProgramAsync", StringComparison.Ordinal), "gcode button uploads through relay client");
    Require(true, mainWindow.Contains("ConfirmProgramOverwriteIfNeededAsync", StringComparison.Ordinal), "gcode upload prompts before overwrite");
    Require(true, mainWindow.Contains("gcode_upload_uploaded", StringComparison.Ordinal), "gcode button records upload evidence");
    Require(true, mainWindow.Contains("OpenSystemGCodeButton_OnClick", StringComparison.Ordinal), "system gcode click handler wired");
    Require(true, mainWindow.Contains("ProgramDirectoryDialog", StringComparison.Ordinal), "system gcode opens directory dialog");
    Require(true, mainWindow.Contains("system_gcode_directory_listed", StringComparison.Ordinal), "system gcode records list evidence");
    Require(true, mainWindow.Contains("system_gcode_file_deleted", StringComparison.Ordinal), "system gcode records delete evidence");
    Require(true, otaWindow.Contains("OtaUpgradeButton_OnClick", StringComparison.Ordinal), "OTA upgrade click handler wired");
    Require(true, otaWindow.Contains("ota_upgrade_requested", StringComparison.Ordinal), "OTA upgrade records request evidence");
    Require(true, otaWindow.Contains("ota_upgrade_response", StringComparison.Ordinal), "OTA upgrade records response evidence");
    Require(true, otaWindow.Contains("dna_private_first_no_public_when_private_present", StringComparison.Ordinal), "OTA upgrade keeps private-first policy");
    Require(true, programDialog.Contains("ListView", StringComparison.Ordinal), "program directory dialog shows file list");
    Require(true, programDialog.Contains("OpenSelectedAsync", StringComparison.Ordinal), "program directory dialog opens file for edit");
    Require(true, programDialog.Contains("SaveEditAsync", StringComparison.Ordinal), "program directory dialog saves edited file");
    Require(true, programDialog.Contains("DeleteSelectedAsync", StringComparison.Ordinal), "program directory dialog deletes selected file");
    Require(false, mainWindow.Contains("gcode_upload_blocked", StringComparison.Ordinal), "gcode button no longer uses placeholder block event");
    Require(true, relayClient.Contains("remote/program/upload", StringComparison.Ordinal), "relay client calls program upload endpoint");
    Require(true, relayClient.Contains("remote/program/list", StringComparison.Ordinal), "relay client calls program list endpoint");
    Require(true, relayClient.Contains("remote/program/file", StringComparison.Ordinal), "relay client calls program file endpoint");
    Require(true, relayClient.Contains("overwrite=", StringComparison.Ordinal), "relay client sends overwrite flag");
    Require(true, relayClient.Contains("remote/ota/upgrade", StringComparison.Ordinal), "relay client calls OTA upgrade endpoint");
    Require(true, mockRelay.Contains("OTA_NOT_IMPLEMENTED", StringComparison.Ordinal), "mock relay rejects OTA until board client exists");
    Require(true, readme.Contains("GET /remote/diagnostics", StringComparison.Ordinal) && readme.Contains("diagnostic summary", StringComparison.Ordinal), "README documents read-log button");
    Require(true, readme.Contains("indented, wrapped", StringComparison.Ordinal), "README documents wrapped diagnostics display");
    Require(true,
        readme.Contains("LinuxCNC/control", StringComparison.Ordinal)
            && readme.Contains("hardware/resource words", StringComparison.Ordinal),
        "README documents diagnostics redaction");
    Require(true, readme.Contains("POST /remote/program/upload", StringComparison.Ordinal), "README documents gcode upload endpoint");
    Require(true, readme.Contains("GET /remote/program/list", StringComparison.Ordinal), "README documents gcode list endpoint");
    Require(true, readme.Contains("DELETE /remote/program/file", StringComparison.Ordinal), "README documents gcode delete endpoint");
    Require(true, readme.Contains("overwrite=1", StringComparison.Ordinal), "README documents overwrite confirmation path");
    Require(true, readme.Contains("POST /remote/ota/upgrade", StringComparison.Ordinal), "README documents OTA upgrade endpoint");
    Require(true, buttonDoc.Contains("GET /remote/diagnostics", StringComparison.Ordinal), "button doc documents diagnostics endpoint");
    Require(true, buttonDoc.Contains("GET /remote/diagnostics", StringComparison.Ordinal), "button doc documents diagnostics wrapping");
    Require(true, logDialog.Contains("[redacted]", StringComparison.Ordinal), "button doc documents diagnostics redaction placeholder");
    Require(true, buttonDoc.Contains("/opt/8ax/phase0_bus5/nc", StringComparison.Ordinal), "button doc documents board program directory");
    Require(true, buttonDoc.Contains("GET /remote/program/list", StringComparison.Ordinal), "button doc documents board program list endpoint");
    Require(true, buttonDoc.Contains("GET /remote/program/file?filename=<name>&content=1", StringComparison.Ordinal), "button doc documents open system gcode button");
    Require(true, buttonDoc.Contains("POST /remote/ota/upgrade", StringComparison.Ordinal), "button doc documents OTA upgrade button");
}

static void VerifyDiagnosticsDisplaySanitization()
{
    MethodInfo formatter = typeof(ClientLogPreviewDialog).GetMethod(
        "BuildDisplayText",
        BindingFlags.NonPublic | BindingFlags.Static)
        ?? throw new MissingMethodException(nameof(ClientLogPreviewDialog), "BuildDisplayText");

    string raw = """
    {
      "collected_at_unix_ms": 1782351854279,
      "max_upload_bytes": 67108864,
      "relay": {
        "latest_frame_id": 3287,
        "input_enabled": true,
        "recent_input_active": false,
        "dirty_events": [
          { "sequence": 1, "frame_id": 1, "x": 10, "y": 20, "width": 30, "height": 40 }
        ]
      },
      "run_files": {
        "remote_info.json": { "exists": true },
        "state_shm_shadow_reader_status.json": { "exists": true },
        "command_broker_status.json": { "exists": true }
      },
      "linuxcnc": "LinuxCNC/HAL/EtherCAT",
      "path": "/dev/shm/v3_status_shm",
      "metrics": "cpu0 memory disk motor encoder phase0_bus5"
    }
    """;
    string summary = (string)(formatter.Invoke(null, new object[] { raw, false }) ?? String.Empty);
    string details = (string)(formatter.Invoke(null, new object[] { raw, true }) ?? String.Empty);

    string[] forbidden =
    [
        "linuxcnc",
        "hal",
        "ethercat",
        "/dev",
        "v3_status_shm",
        "shm",
        "cpu",
        "memory",
        "disk",
        "motor",
        "encoder",
        "phase0_bus5",
    ];
    foreach (string token in forbidden)
    {
        Require(false, summary.Contains(token, StringComparison.OrdinalIgnoreCase), $"diagnostics summary hides {token}");
        Require(false, details.Contains(token, StringComparison.OrdinalIgnoreCase), $"diagnostics details hide {token}");
    }

    Require(true, summary.Length > 0, "diagnostics summary is default operator view");
    Require(true, summary.Contains(Environment.NewLine, StringComparison.Ordinal), "diagnostics summary includes status section");
    Require(true, details.Contains("[redacted]", StringComparison.Ordinal), "diagnostics details include redaction placeholder");
    Require(true, details.Contains(Environment.NewLine, StringComparison.Ordinal), "diagnostics details remain multi-line");
}

static void VerifySingleInstanceContract()
{
    string repoRoot = FindRepoRoot(AppContext.BaseDirectory);
    string app = File.ReadAllText(Path.Combine(repoRoot, "8ax-win", "src", "8ax.WinRemote", "App.xaml.cs"));
    Require(true, app.Contains(@"Local\EightAxis.WinRemote.SingleInstance", StringComparison.Ordinal), "single-instance mutex name");
    Require(true, app.Contains("new Mutex(initiallyOwned: true", StringComparison.Ordinal), "single-instance mutex acquired on startup");
    Require(true, app.Contains("Shutdown(0)", StringComparison.Ordinal), "second instance exits before opening window");
    Require(true, app.Contains("_ownsSingleInstanceMutex", StringComparison.Ordinal), "single-instance ownership tracked");
    Require(true, app.Contains("ReleaseMutex()", StringComparison.Ordinal), "single-instance mutex released on exit");
}

static void VerifySingleExePublishContract()
{
    string repoRoot = FindRepoRoot(AppContext.BaseDirectory);
    string scriptPath = Path.Combine(repoRoot, "8ax-win", "tools", "publish_winremote_update.ps1");
    Require(true, File.Exists(scriptPath), "release publish script exists");
    string script = File.ReadAllText(scriptPath);
    string winReadme = File.ReadAllText(Path.Combine(repoRoot, "8ax-win", "README.md"));
    string uploadRule = winReadme;
    Require(true, script.Contains("-p:PublishSingleFile=true", StringComparison.Ordinal), "release uses single-exe publish flag");
    Require(true, script.Contains("--self-contained true", StringComparison.Ordinal), "release exe is self-contained");
    Require(true, script.Contains("Join-Path $repo \"8ax-win\\publish\\$Runtime\"", StringComparison.Ordinal), "single-exe official output directory is repo-relative");
    Require(true, script.Contains("8ax.WinRemote.exe", StringComparison.Ordinal), "single-exe fixed name");
    Require(true, script.Contains("DebugType=embedded", StringComparison.Ordinal), "single-exe embeds debug info");
    Require(true, script.Contains("-p:InformationalVersion=$Version", StringComparison.Ordinal), "release embeds exact version");
    Require(true, script.Contains("-p:FileVersion=$assemblyVersion", StringComparison.Ordinal), "release embeds file version");
    Require(true, script.Contains("update package must contain only", StringComparison.Ordinal), "release package is single-file only");
    Require(true, script.Contains("multi-file sidecar must not be published", StringComparison.Ordinal), "release blocks sidecar files");
    Require(true, script.Contains("Formal release path: package generation must be followed by direct VPS upload and verification", StringComparison.Ordinal), "release uploads immediately after package generation");
    Require(true, script.Contains("sha256sum '$remotePackagePath'", StringComparison.Ordinal), "release verifies remote VPS package hash");
    Require(true, script.Contains("Assert-ManifestMatchesRelease -Json $remoteManifestJson", StringComparison.Ordinal), "release verifies remote VPS manifest file");
    Require(true, script.Contains("UTF8Encoding($false)", StringComparison.Ordinal), "release writes manifest without UTF-8 BOM");
    Require(true, script.Contains("TrimStart([char]0xFEFF)", StringComparison.Ordinal), "release manifest verification strips UTF-8 BOM");
    Require(true, script.Contains("[char]0x00EF", StringComparison.Ordinal), "release manifest verification strips mojibake UTF-8 BOM");
    Require(true, script.Contains("Invoke-WebRequest -UseBasicParsing -TimeoutSec 15", StringComparison.Ordinal), "release verifies HTTPS manifest URLs");
    Require(true, script.Contains("VPS HTTPS manifest verification failed", StringComparison.Ordinal), "release fails when VPS manifest verification fails");
    Require(true, script.Contains("-SkipUpload is diagnostic only", StringComparison.Ordinal), "skip upload is diagnostic-only");
    Require(true, winReadme.Contains("must not stop after local package generation", StringComparison.Ordinal), "README requires direct VPS upload after generation");
    Require(true, uploadRule.Contains("must not stop after local package generation", StringComparison.Ordinal), "VPS doc requires direct upload after generation");
    Require(false, script.Contains("--self-contained false", StringComparison.Ordinal), "release no longer uses multi-file framework-dependent publish");
}

static async Task VerifyMockRelayReadOnlyClientAsync()
{
    string repoRoot = FindRepoRoot(AppContext.BaseDirectory);
    string mockRelayProject = Path.Combine(repoRoot, "8ax-win", "tools", "mock-relay", "8ax.MockRelay.csproj");
    if (!File.Exists(mockRelayProject))
    {
        throw new FileNotFoundException("mock relay project not found", mockRelayProject);
    }

    int port = FindFreeTcpPort();
    Uri relayUri = new($"http://127.0.0.1:{port}/");
    using Process relay = StartMockRelay(mockRelayProject, relayUri);
    try
    {
        await WaitForRelayAsync(relay, relayUri);

        using RemoteRelayClient client = new(relayUri);
        RemoteInfoMessage info = await client.GetInfoAsync(CancellationToken.None);
        Require(PointerMapper.RemoteWidth, info.Width, "mock relay info width");
        Require(PointerMapper.RemoteHeight, info.Height, "mock relay info height");
        Require(true, info.ViewOnly, "mock relay view-only");
        Require(10.0, info.SystemMetrics?.Cpu0Percent ?? -1.0, "mock relay cpu0 metric");
        Require(11.0, info.SystemMetrics?.Cpu1Percent ?? -1.0, "mock relay cpu1 metric");
        Require(42.0, info.SystemMetrics?.MemoryPercent ?? -1.0, "mock relay memory metric");
        Require(64.0, info.SystemMetrics?.DiskPercent ?? -1.0, "mock relay disk metric");

        string diagnostics = await client.GetDiagnosticsJsonAsync(CancellationToken.None);
        Require(true, diagnostics.Contains("\"schema\":\"re.v3.remote_diagnostics.v1\"", StringComparison.Ordinal), "mock relay diagnostics schema");
        Require(true, diagnostics.Contains("\"program_dir\":\"/opt/8ax/phase0_bus5/nc\"", StringComparison.Ordinal), "mock relay diagnostics program dir");

        OtaUpgradeResult ota = await client.RequestOtaUpgradeAsync(CancellationToken.None);
        Require("rejected", ota.Status, "mock relay OTA status");
        Require("OTA_NOT_IMPLEMENTED", ota.Code, "mock relay OTA code");
        Require(false, ota.Cancellable, "mock relay OTA job not cancellable because no job started");

        byte[] program = Encoding.ASCII.GetBytes("G0 X0\nM2\n");
        using MemoryStream programStream = new(program);
        string programSha = Sha256Hex(program);
        ProgramUploadResult upload = await client.UploadProgramAsync("unit_test.ngc", programStream, program.Length, programSha, overwrite: false, CancellationToken.None);
        Require("unit_test.ngc", upload.FileName, "mock relay upload filename");
        Require(program.Length, (int)upload.SizeBytes, "mock relay upload size");
        Require(programSha, upload.Sha256, "mock relay upload sha");
        Require("/opt/8ax/phase0_bus5/nc/unit_test.ngc", upload.DestinationPath, "mock relay upload destination");

        ProgramListResult list = await client.GetProgramListAsync(CancellationToken.None);
        Require(1, list.Count, "mock relay program list count");
        Require("unit_test.ngc", list.Files.Single().FileName, "mock relay program list filename");
        ProgramFileInfo stat = await client.GetProgramFileInfoAsync("unit_test.ngc", CancellationToken.None);
        Require(true, stat.Exists, "mock relay program stat exists");
        Require(programSha, stat.Sha256 ?? String.Empty, "mock relay program stat sha");
        ProgramFileContentResult content = await client.GetProgramFileContentAsync("unit_test.ngc", CancellationToken.None);
        Require("G0 X0\nM2\n", content.Text, "mock relay program read text");

        bool duplicateRejected = false;
        try
        {
            using MemoryStream duplicateStream = new(program);
            _ = await client.UploadProgramAsync("unit_test.ngc", duplicateStream, program.Length, programSha, overwrite: false, CancellationToken.None);
        }
        catch (HttpRequestException)
        {
            duplicateRejected = true;
        }
        Require(true, duplicateRejected, "mock relay duplicate upload rejected without overwrite");

        byte[] editedProgram = Encoding.ASCII.GetBytes("G1 X1\nM2\n");
        using MemoryStream editedStream = new(editedProgram);
        string editedSha = Sha256Hex(editedProgram);
        ProgramUploadResult overwrite = await client.UploadProgramAsync("unit_test.ngc", editedStream, editedProgram.Length, editedSha, overwrite: true, CancellationToken.None);
        Require(true, overwrite.Overwrote, "mock relay upload overwrite flag");
        Require(editedSha, overwrite.Sha256, "mock relay overwritten sha");
        ProgramDeleteResult deleted = await client.DeleteProgramFileAsync("unit_test.ngc", CancellationToken.None);
        Require(true, deleted.Deleted, "mock relay program delete result");
        ProgramFileInfo missing = await client.GetProgramFileInfoAsync("unit_test.ngc", CancellationToken.None);
        Require(false, missing.Exists, "mock relay program stat missing after delete");

        RemoteFramebuffer framebuffer = new();
        RemoteFrameAssembler assembler = new(framebuffer);
        RemoteFrameApplyResult full = assembler.Apply(await client.GetFullFrameAsync(CancellationToken.None));
        Require(RemoteFrameApplyStatus.AppliedFullFrame, full.Status, $"mock relay full frame apply {full.Reason}");

        bool appliedDirty = false;
        bool neededFullFrame = false;
        using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(10));
        await foreach (RemoteFramePacket packet in client.ReadFrameStreamAsync(timeout.Token))
        {
            RemoteFrameApplyResult result = assembler.Apply(packet);
            if (result.Status == RemoteFrameApplyStatus.AppliedDirtyRects)
            {
                appliedDirty = true;
            }
            else if (result.Status == RemoteFrameApplyStatus.NeedFullFrame)
            {
                neededFullFrame = true;
                RemoteFrameApplyResult repair = assembler.Apply(await client.GetFullFrameAsync(timeout.Token));
                Require(RemoteFrameApplyStatus.AppliedFullFrame, repair.Status, $"mock relay full-frame repair {repair.Reason}");
                break;
            }
        }

        Require(true, appliedDirty, "mock relay applied at least one dirty rect");
        Require(true, neededFullFrame, "mock relay drop triggers full-frame repair");
    }
    finally
    {
        StopProcessTree(relay);
    }
}

static string Sha256Hex(byte[] payload) =>
    Convert.ToHexString(SHA256.HashData(payload)).ToLowerInvariant();

static byte[] FullBgraFrame(byte blue, byte green, byte red) =>
    SolidBgraRect(PointerMapper.RemoteWidth, PointerMapper.RemoteHeight, blue, green, red);

static byte[] SolidBgraRect(int width, int height, byte blue, byte green, byte red)
{
    byte[] pixels = new byte[width * height * 4];
    for (int i = 0; i < pixels.Length; i += 4)
    {
        pixels[i] = blue;
        pixels[i + 1] = green;
        pixels[i + 2] = red;
        pixels[i + 3] = 255;
    }

    return pixels;
}

static int PixelOffset(int x, int y) => ((y * PointerMapper.RemoteWidth) + x) * 4;

static Process StartMockRelay(string mockRelayProject, Uri relayUri)
{
    ProcessStartInfo startInfo = new("dotnet")
    {
        UseShellExecute = false,
        CreateNoWindow = true,
        RedirectStandardOutput = true,
        RedirectStandardError = true,
    };
    startInfo.ArgumentList.Add("run");
    startInfo.ArgumentList.Add("--project");
    startInfo.ArgumentList.Add(mockRelayProject);
    startInfo.ArgumentList.Add("--");
    startInfo.ArgumentList.Add("--prefix");
    startInfo.ArgumentList.Add(relayUri.ToString());
    startInfo.ArgumentList.Add("--drop-every");
    startInfo.ArgumentList.Add("2");

    Process process = new() { StartInfo = startInfo };
    process.Start();
    return process;
}

static async Task WaitForRelayAsync(Process relay, Uri relayUri)
{
    using RemoteRelayClient client = new(relayUri);
    DateTime deadline = DateTime.UtcNow.AddSeconds(20);
    Exception? lastError = null;
    while (DateTime.UtcNow < deadline)
    {
        if (relay.HasExited)
        {
            string stdout = await relay.StandardOutput.ReadToEndAsync();
            string stderr = await relay.StandardError.ReadToEndAsync();
            throw new InvalidOperationException($"mock relay exited early with code {relay.ExitCode}. stdout={stdout} stderr={stderr}");
        }

        try
        {
            _ = await client.GetInfoAsync(CancellationToken.None);
            return;
        }
        catch (Exception ex) when (ex is HttpRequestException or SocketException or InvalidOperationException)
        {
            lastError = ex;
            await Task.Delay(250);
        }
    }

    throw new TimeoutException($"mock relay did not start at {relayUri}: {lastError?.Message}");
}

static void StopProcessTree(Process process)
{
    try
    {
        if (!process.HasExited)
        {
            process.Kill(entireProcessTree: true);
            process.WaitForExit(5000);
        }
    }
    catch (InvalidOperationException)
    {
    }
}

static int FindFreeTcpPort()
{
    TcpListener listener = new(IPAddress.Loopback, 0);
    listener.Start();
    try
    {
        return ((IPEndPoint)listener.LocalEndpoint).Port;
    }
    finally
    {
        listener.Stop();
    }
}

static string FindRepoRoot(string startDirectory)
{
    DirectoryInfo? directory = new(startDirectory);
    while (directory is not null)
    {
        string candidate = Path.Combine(directory.FullName, "8ax-win", "tools", "mock-relay", "8ax.MockRelay.csproj");
        if (File.Exists(candidate))
        {
            return directory.FullName;
        }

        directory = directory.Parent;
    }

    throw new DirectoryNotFoundException("Could not locate repo root for mock relay test.");
}

static string FindWinRemoteRoot(string startDirectory)
{
    DirectoryInfo? directory = new(startDirectory);
    while (directory is not null)
    {
        string candidate = Path.Combine(directory.FullName, "src", "8ax.WinRemote", "8ax.WinRemote.csproj");
        if (File.Exists(candidate))
        {
            return directory.FullName;
        }

        directory = directory.Parent;
    }

    string repoRoot = FindRepoRoot(startDirectory);
    string winRoot = Path.Combine(repoRoot, "8ax-win");
    if (File.Exists(Path.Combine(winRoot, "src", "8ax.WinRemote", "8ax.WinRemote.csproj")))
    {
        return winRoot;
    }

    throw new DirectoryNotFoundException("Could not locate 8ax-win root.");
}

static string ReadWinRemoteFile(string winRoot, params string[] relativeParts)
{
    string[] parts = new string[relativeParts.Length + 1];
    parts[0] = winRoot;
    Array.Copy(relativeParts, 0, parts, 1, relativeParts.Length);
    return File.ReadAllText(Path.Combine(parts));
}
