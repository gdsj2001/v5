using System.Diagnostics;
using System.Net;
using System.Net.WebSockets;
using System.Security.Cryptography;
using System.Text;
using EightAxis.WinRemote.Protocol;

MockRelayOptions options = MockRelayOptions.Parse(args);
using CancellationTokenSource shutdown = new();
Console.CancelKeyPress += (_, eventArgs) =>
{
    eventArgs.Cancel = true;
    shutdown.Cancel();
};

MockRelayServer server = new(options);
await server.RunAsync(shutdown.Token);

internal sealed record MockRelayOptions(string Prefix, int DropEvery)
{
    public static MockRelayOptions Parse(IReadOnlyList<string> args)
    {
        string prefix = ReadOption(args, "--prefix") ?? "http://127.0.0.1:18080/";
        if (!prefix.EndsWith("/", StringComparison.Ordinal))
        {
            prefix += "/";
        }

        int dropEvery = 0;
        string? dropEveryText = ReadOption(args, "--drop-every");
        if (!String.IsNullOrWhiteSpace(dropEveryText))
        {
            _ = Int32.TryParse(dropEveryText, out dropEvery);
        }

        return new MockRelayOptions(prefix, Math.Max(0, dropEvery));
    }

    private static string? ReadOption(IReadOnlyList<string> args, string optionName)
    {
        for (int i = 0; i < args.Count; i++)
        {
            if (String.Equals(args[i], optionName, StringComparison.OrdinalIgnoreCase) && i + 1 < args.Count)
            {
                return args[i + 1];
            }

            string prefix = optionName + "=";
            if (args[i].StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            {
                return args[i][prefix.Length..];
            }
        }

        return null;
    }
}

internal sealed class MockRelayServer
{
    private const int Width = 1024;
    private const int Height = 600;
    private const string PixelFormat = RemotePixelFormats.Bgra32;
    private const int Stride = Width * 4;
    private const int StreamTargetFps = 30;
    private static readonly long StreamFrameIntervalTicks = (long)Math.Round(Stopwatch.Frequency / (double)StreamTargetFps);
    private const string ProgramDir = "/opt/8ax/phase0_bus5/nc";
    private const long ProgramEditMaxBytes = 1024 * 1024;
    private readonly FrameStore _frameStore = new(Width, Height);
    private readonly HttpListener _listener = new();
    private readonly MockRelayOptions _options;
    private readonly Dictionary<string, MockProgramFile> _uploads = new(StringComparer.OrdinalIgnoreCase);

    public MockRelayServer(MockRelayOptions options)
    {
        _options = options;
        _listener.Prefixes.Add(options.Prefix);
    }

    public async Task RunAsync(CancellationToken cancellationToken)
    {
        _listener.Start();
        Console.WriteLine($"8ax mock relay listening on {_options.Prefix}");
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                HttpListenerContext context = await _listener.GetContextAsync().WaitAsync(cancellationToken);
                _ = Task.Run(() => HandleContextAsync(context, cancellationToken), cancellationToken);
            }
        }
        catch (OperationCanceledException)
        {
        }
        finally
        {
            _listener.Stop();
        }
    }

    private async Task HandleContextAsync(HttpListenerContext context, CancellationToken cancellationToken)
    {
        try
        {
            string path = context.Request.Url?.AbsolutePath.TrimEnd('/') ?? String.Empty;
            if (path.Equals("/remote/info", StringComparison.OrdinalIgnoreCase))
            {
                await WriteJsonAsync(context.Response, new RemoteInfoMessage(
                    "8ax-remote-ui/1",
                    Width,
                    Height,
                    PixelFormat,
                    Stride,
                    true,
                    new RemoteSystemMetrics(10.0, 11.0, 42.0, 64.0, 420L * 1024 * 1024, 1000L * 1024 * 1024, 64L * 1024 * 1024 * 1024, 100L * 1024 * 1024 * 1024)),
                    cancellationToken);
                return;
            }

            if (path.Equals("/remote/frame/full", StringComparison.OrdinalIgnoreCase))
            {
                FrameSnapshot fullFrame = _frameStore.GetFullFrame();
                FrameMetadata metadata = FrameMetadata.FullFrame(fullFrame.FrameId, Width, Height, Stride, PixelFormat);
                byte[] envelope = RemoteProtocolJson.EncodeFrameEnvelope(metadata, fullFrame.Payload);
                await WriteBytesAsync(context.Response, "application/octet-stream", envelope, cancellationToken);
                return;
            }

            if (path.Equals("/remote/diagnostics", StringComparison.OrdinalIgnoreCase))
            {
                await WriteJsonAsync(context.Response, new
                {
                    Schema = "re.v3.remote_diagnostics.v1",
                    ProtocolVersion = "8ax-remote-ui/1",
                    CollectedAtUnixMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                    ProgramDir,
                    UploadCount = _uploads.Count,
                    Info = new RemoteInfoMessage(
                        "8ax-remote-ui/1",
                        Width,
                        Height,
                        PixelFormat,
                        Stride,
                        true,
                        new RemoteSystemMetrics(10.0, 11.0, 42.0, 64.0, 420L * 1024 * 1024, 1000L * 1024 * 1024, 64L * 1024 * 1024 * 1024, 100L * 1024 * 1024 * 1024)),
                }, cancellationToken);
                return;
            }

            if (path.Equals("/remote/program/list", StringComparison.OrdinalIgnoreCase) && context.Request.HttpMethod.Equals("GET", StringComparison.OrdinalIgnoreCase))
            {
                await HandleProgramListAsync(context, cancellationToken);
                return;
            }

            if (path.Equals("/remote/program/file", StringComparison.OrdinalIgnoreCase))
            {
                await HandleProgramFileAsync(context, cancellationToken);
                return;
            }

            if (path.Equals("/remote/program/upload", StringComparison.OrdinalIgnoreCase) && context.Request.HttpMethod.Equals("POST", StringComparison.OrdinalIgnoreCase))
            {
                await HandleProgramUploadAsync(context, cancellationToken);
                return;
            }

            if (path.Equals("/remote/ota/upgrade", StringComparison.OrdinalIgnoreCase) && context.Request.HttpMethod.Equals("POST", StringComparison.OrdinalIgnoreCase))
            {
                await WriteJsonAsync(context.Response, new OtaUpgradeResult(
                    "re.v3.remote_ota_upgrade.v1",
                    "rejected",
                    "OTA_NOT_IMPLEMENTED",
                    "Board OTA client is not implemented; no upgrade job was started.",
                    null,
                    null,
                    false,
                    DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()), cancellationToken);
                return;
            }

            if (path.Equals("/remote/stream", StringComparison.OrdinalIgnoreCase) && context.Request.IsWebSocketRequest)
            {
                HttpListenerWebSocketContext socketContext = await context.AcceptWebSocketAsync(subProtocol: null);
                await StreamFramesAsync(socketContext.WebSocket, cancellationToken);
                return;
            }

            context.Response.StatusCode = 404;
            context.Response.Close();
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            if (context.Response.OutputStream.CanWrite)
            {
                byte[] error = Encoding.UTF8.GetBytes(ex.Message);
                context.Response.StatusCode = 500;
                await context.Response.OutputStream.WriteAsync(error, cancellationToken);
            }

            context.Response.Close();
        }
    }

    private async Task HandleProgramUploadAsync(HttpListenerContext context, CancellationToken cancellationToken)
    {
        string fileName = context.Request.QueryString["filename"] ?? String.Empty;
        if (!IsSafeProgramFileName(fileName))
        {
            await WriteErrorAsync(context.Response, 400, "invalid filename", cancellationToken);
            return;
        }

        bool overwrite = String.Equals(context.Request.QueryString["overwrite"], "1", StringComparison.Ordinal)
            || String.Equals(context.Request.QueryString["overwrite"], "true", StringComparison.OrdinalIgnoreCase);
        if (_uploads.ContainsKey(fileName) && !overwrite)
        {
            await WriteErrorAsync(context.Response, 409, "program file exists; retry with overwrite=1", cancellationToken);
            return;
        }

        using MemoryStream stream = new();
        await context.Request.InputStream.CopyToAsync(stream, cancellationToken);
        byte[] payload = stream.ToArray();
        string sha256 = Convert.ToHexString(SHA256.HashData(payload)).ToLowerInvariant();
        bool overwrote = _uploads.ContainsKey(fileName);
        _uploads[fileName] = new MockProgramFile(fileName, payload, sha256, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());
        await WriteJsonAsync(
            context.Response,
            new ProgramUploadResult(
                "re.v3.remote_program_upload.v1",
                fileName,
                ProgramDir + "/" + fileName,
                payload.Length,
                sha256,
                overwrote,
                DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()),
            cancellationToken);
    }

    private async Task HandleProgramListAsync(HttpListenerContext context, CancellationToken cancellationToken)
    {
        ProgramFileInfo[] files = _uploads.Values
            .OrderBy(item => item.FileName, StringComparer.OrdinalIgnoreCase)
            .Select(ToProgramFileInfo)
            .ToArray();
        await WriteJsonAsync(
            context.Response,
            new ProgramListResult("re.v3.remote_program_list.v1", ProgramDir, files.Length, files),
            cancellationToken);
    }

    private async Task HandleProgramFileAsync(HttpListenerContext context, CancellationToken cancellationToken)
    {
        string fileName = context.Request.QueryString["filename"] ?? String.Empty;
        if (!IsSafeProgramFileName(fileName))
        {
            await WriteErrorAsync(context.Response, 400, "invalid filename", cancellationToken);
            return;
        }

        if (context.Request.HttpMethod.Equals("DELETE", StringComparison.OrdinalIgnoreCase))
        {
            _uploads.TryGetValue(fileName, out MockProgramFile? previous);
            bool deleted = _uploads.Remove(fileName);
            await WriteJsonAsync(
                context.Response,
                new ProgramDeleteResult(
                    "re.v3.remote_program_delete.v1",
                    fileName,
                    ProgramDir + "/" + fileName,
                    deleted,
                    DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                    previous is null ? null : ToProgramFileInfo(previous)),
                cancellationToken);
            return;
        }

        if (!context.Request.HttpMethod.Equals("GET", StringComparison.OrdinalIgnoreCase))
        {
            await WriteErrorAsync(context.Response, 405, "method not allowed", cancellationToken);
            return;
        }

        if (!_uploads.TryGetValue(fileName, out MockProgramFile? file))
        {
            await WriteJsonAsync(
                context.Response,
                new ProgramFileInfo(fileName, ProgramDir + "/" + fileName, Exists: false),
                cancellationToken);
            return;
        }

        bool withContent = String.Equals(context.Request.QueryString["content"], "1", StringComparison.Ordinal)
            || String.Equals(context.Request.QueryString["content"], "true", StringComparison.OrdinalIgnoreCase);
        if (!withContent)
        {
            await WriteJsonAsync(context.Response, ToProgramFileInfo(file), cancellationToken);
            return;
        }

        string text = Encoding.UTF8.GetString(file.Payload);
        await WriteJsonAsync(
            context.Response,
            new ProgramFileContentResult(
                "re.v3.remote_program_file.v1",
                file.FileName,
                ProgramDir + "/" + file.FileName,
                file.Payload.Length,
                file.Sha256,
                file.ModifiedAtUnixMs,
                text),
            cancellationToken);
    }

    private static ProgramFileInfo ToProgramFileInfo(MockProgramFile file) =>
        new(
            file.FileName,
            ProgramDir + "/" + file.FileName,
            Exists: true,
            SizeBytes: file.Payload.Length,
            ModifiedAtUnixMs: file.ModifiedAtUnixMs,
            Editable: file.Payload.Length <= ProgramEditMaxBytes,
            Sha256: file.Sha256);

    private static bool IsSafeProgramFileName(string fileName)
    {
        if (String.IsNullOrWhiteSpace(fileName) || fileName != Path.GetFileName(fileName) || fileName.Contains("..", StringComparison.Ordinal))
        {
            return false;
        }

        string extension = Path.GetExtension(fileName);
        return extension.Equals(".ngc", StringComparison.OrdinalIgnoreCase)
            || extension.Equals(".nc", StringComparison.OrdinalIgnoreCase)
            || extension.Equals(".tap", StringComparison.OrdinalIgnoreCase)
            || extension.Equals(".gcode", StringComparison.OrdinalIgnoreCase);
    }

    private async Task StreamFramesAsync(WebSocket socket, CancellationToken cancellationToken)
    {
        FrameSnapshot fullFrame = _frameStore.GetFullFrame();
        await SendFrameAsync(socket, FrameMetadata.FullFrame(fullFrame.FrameId, Width, Height, Stride, PixelFormat), fullFrame.Payload, cancellationToken);

        int sentCandidate = 0;
        long nextFrameTicks = Stopwatch.GetTimestamp();
        while (!cancellationToken.IsCancellationRequested && socket.State == WebSocketState.Open)
        {
            nextFrameTicks = await PaceToNextStreamFrameAsync(nextFrameTicks, cancellationToken);
            DirtyFrame dirty = _frameStore.NextDirtyFrame();
            sentCandidate++;
            if (_options.DropEvery > 0 && sentCandidate % _options.DropEvery == 0)
            {
                continue;
            }

            FrameMetadata metadata = FrameMetadata.DirtyRects(
                dirty.FrameId,
                dirty.BaseFrameId,
                Width,
                Height,
                Stride,
                PixelFormat,
                new[] { dirty.Rect });
            await SendFrameAsync(socket, metadata, dirty.Payload, cancellationToken);
        }
    }

    private static async Task<long> PaceToNextStreamFrameAsync(long previousTargetTicks, CancellationToken cancellationToken)
    {
        long targetTicks = previousTargetTicks + StreamFrameIntervalTicks;
        long nowTicks = Stopwatch.GetTimestamp();
        if (targetTicks < nowTicks - StreamFrameIntervalTicks)
        {
            targetTicks = nowTicks + StreamFrameIntervalTicks;
        }

        while (true)
        {
            long remainingTicks = targetTicks - Stopwatch.GetTimestamp();
            if (remainingTicks <= 0)
            {
                return targetTicks;
            }

            double remainingMs = remainingTicks * 1000.0 / Stopwatch.Frequency;
            if (remainingMs > 3.0)
            {
                await Task.Delay(TimeSpan.FromMilliseconds(Math.Max(1.0, remainingMs - 1.0)), cancellationToken);
            }
            else
            {
                await Task.Yield();
            }
        }
    }

    private static async Task SendFrameAsync(WebSocket socket, FrameMetadata metadata, byte[] payload, CancellationToken cancellationToken)
    {
        byte[] metadataBytes = Encoding.UTF8.GetBytes(RemoteProtocolJson.Serialize(metadata));
        await socket.SendAsync(metadataBytes, WebSocketMessageType.Text, true, cancellationToken);
        await socket.SendAsync(payload, WebSocketMessageType.Binary, true, cancellationToken);
    }

    private static async Task WriteJsonAsync<T>(HttpListenerResponse response, T body, CancellationToken cancellationToken)
    {
        byte[] payload = Encoding.UTF8.GetBytes(RemoteProtocolJson.Serialize(body));
        await WriteBytesAsync(response, "application/json; charset=utf-8", payload, cancellationToken);
    }

    private static async Task WriteBytesAsync(HttpListenerResponse response, string contentType, byte[] payload, CancellationToken cancellationToken)
    {
        response.StatusCode = 200;
        response.ContentType = contentType;
        response.ContentLength64 = payload.Length;
        await response.OutputStream.WriteAsync(payload, cancellationToken);
        response.Close();
    }

    private static async Task WriteErrorAsync(HttpListenerResponse response, int statusCode, string message, CancellationToken cancellationToken)
    {
        byte[] payload = Encoding.UTF8.GetBytes(message);
        response.StatusCode = statusCode;
        response.ContentType = "text/plain; charset=utf-8";
        response.ContentLength64 = payload.Length;
        await response.OutputStream.WriteAsync(payload, cancellationToken);
        response.Close();
    }
}

internal sealed class FrameStore
{
    private readonly byte[] _bgra32;
    private readonly int _width;
    private readonly int _height;
    private long _frameId = 1;
    private int _tick;

    public FrameStore(int width, int height)
    {
        _width = width;
        _height = height;
        _bgra32 = new byte[width * height * 4];
        FillInitialFrame();
    }

    public FrameSnapshot GetFullFrame() => new(_frameId, _bgra32.ToArray());

    public DirtyFrame NextDirtyFrame()
    {
        long baseFrameId = _frameId;
        int x = 32 + ((_tick * 37) % (_width - 160));
        int y = 28 + ((_tick * 23) % (_height - 120));
        int w = 128;
        int h = 48;
        byte blue = (byte)(48 + ((_tick * 17) % 160));
        byte green = (byte)(96 + ((_tick * 11) % 120));
        byte red = (byte)(160 + ((_tick * 7) % 80));
        byte[] rectPayload = new byte[w * h * 4];

        for (int row = 0; row < h; row++)
        {
            int frameOffset = (((y + row) * _width) + x) * 4;
            int rectOffset = row * w * 4;
            for (int col = 0; col < w; col++)
            {
                int frameIndex = frameOffset + (col * 4);
                int rectIndex = rectOffset + (col * 4);
                _bgra32[frameIndex] = blue;
                _bgra32[frameIndex + 1] = green;
                _bgra32[frameIndex + 2] = red;
                _bgra32[frameIndex + 3] = 255;
                rectPayload[rectIndex] = blue;
                rectPayload[rectIndex + 1] = green;
                rectPayload[rectIndex + 2] = red;
                rectPayload[rectIndex + 3] = 255;
            }
        }

        _tick++;
        _frameId++;
        return new DirtyFrame(_frameId, baseFrameId, new DirtyRectMetadata(x, y, w, h, "raw"), rectPayload);
    }

    private void FillInitialFrame()
    {
        for (int y = 0; y < _height; y++)
        {
            for (int x = 0; x < _width; x++)
            {
                int index = ((y * _width) + x) * 4;
                _bgra32[index] = (byte)(20 + (x % 64));
                _bgra32[index + 1] = (byte)(26 + (y % 72));
                _bgra32[index + 2] = (byte)(34 + ((x + y) % 48));
                _bgra32[index + 3] = 255;
            }
        }
    }
}

internal sealed record FrameSnapshot(long FrameId, byte[] Payload);

internal sealed record DirtyFrame(long FrameId, long BaseFrameId, DirtyRectMetadata Rect, byte[] Payload);

internal sealed record MockProgramFile(string FileName, byte[] Payload, string Sha256, long ModifiedAtUnixMs);
