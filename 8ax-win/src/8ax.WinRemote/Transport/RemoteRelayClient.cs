using System.IO;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.WebSockets;
using System.Runtime.CompilerServices;
using System.Text;
using EightAxis.WinRemote.Protocol;

namespace EightAxis.WinRemote.Transport;

public sealed class RemoteRelayClient : IRemoteTransport, IDisposable
{
    private static readonly TimeSpan StreamConnectTimeout = TimeSpan.FromSeconds(3);
    private static readonly TimeSpan InputConnectTimeout = TimeSpan.FromSeconds(2);
    private static readonly TimeSpan NetworkTimeProbeTimeout = TimeSpan.FromMilliseconds(700);
    private static readonly TimeSpan NetworkTimeCacheDuration = TimeSpan.FromMinutes(5);
    private static readonly TimeSpan NetworkTimeRetryDelay = TimeSpan.FromMinutes(1);
    private const string ClientTimeUnixMsHeader = "X-8ax-Client-Time-Unix-Ms";
    private const string ClientTimeSourceHeader = "X-8ax-Client-Time-Source";
    private const string ClientTimeNetworkSource = "winremote-network-time";
    private const string ClientTimeLocalSource = "winremote-local-time";
    private static readonly Uri[] NetworkTimeUris =
    [
        new("https://license.cjwsjzyy.xyz/8ax-winremote/win-x64/manifest.json"),
        new("https://license.3dtouch.top/8ax-winremote/win-x64/manifest.json"),
    ];
    private static readonly HttpClient NetworkTimeHttpClient = new();
    private static readonly SemaphoreSlim NetworkTimeGate = new(1, 1);
    private static TimeSpan? s_networkTimeOffset;
    private static DateTimeOffset s_networkTimeOffsetUpdatedUtc = DateTimeOffset.MinValue;
    private static DateTimeOffset s_lastNetworkTimeFailureUtc = DateTimeOffset.MinValue;

    private readonly HttpClient _httpClient = new(new HttpClientHandler
    {
        UseProxy = false,
    });
    private readonly SemaphoreSlim _inputGate = new(1, 1);
    private ClientWebSocket? _inputSocket;
    private string? _inputSessionId;

    public RemoteRelayClient(Uri baseUri)
    {
        BaseUri = baseUri;
    }

    public Uri BaseUri { get; }

    public async Task<RemoteInfoMessage> GetInfoAsync(CancellationToken cancellationToken)
    {
        ClientTimeStamp clientTime = await ResolveClientTimeAsync(cancellationToken).ConfigureAwait(false);
        using HttpRequestMessage request = new(HttpMethod.Get, new Uri(BaseUri, "remote/info"));
        request.Headers.TryAddWithoutValidation(ClientTimeUnixMsHeader, clientTime.UnixTimeMs.ToString(System.Globalization.CultureInfo.InvariantCulture));
        request.Headers.TryAddWithoutValidation(ClientTimeSourceHeader, clientTime.Source);
        using HttpResponseMessage response = await _httpClient.SendAsync(request, cancellationToken).ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        string json = await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
        return RemoteProtocolJson.Deserialize<RemoteInfoMessage>(json);
    }

    public async Task<RemoteFramePacket> GetFullFrameAsync(CancellationToken cancellationToken)
    {
        using HttpResponseMessage response = await _httpClient.GetAsync(new Uri(BaseUri, "remote/frame/full"), cancellationToken).ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        byte[] envelope = await response.Content.ReadAsByteArrayAsync(cancellationToken).ConfigureAwait(false);
        return RemoteProtocolJson.DecodeFrameEnvelope(envelope);
    }

    public async Task<string> GetDiagnosticsJsonAsync(CancellationToken cancellationToken)
    {
        using HttpResponseMessage response = await _httpClient.GetAsync(new Uri(BaseUri, "remote/diagnostics"), cancellationToken).ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        return await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
    }

    public async Task<ProgramListResult> GetProgramListAsync(CancellationToken cancellationToken)
    {
        using HttpResponseMessage response = await _httpClient.GetAsync(new Uri(BaseUri, "remote/program/list"), cancellationToken).ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        string json = await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
        return RemoteProtocolJson.Deserialize<ProgramListResult>(json);
    }

    public async Task<ProgramFileInfo> GetProgramFileInfoAsync(string fileName, CancellationToken cancellationToken)
    {
        string safeFileName = SafeProgramFileName(fileName);
        using HttpResponseMessage response = await _httpClient.GetAsync(new Uri(BaseUri, $"remote/program/file?filename={Uri.EscapeDataString(safeFileName)}"), cancellationToken).ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        string json = await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
        return RemoteProtocolJson.Deserialize<ProgramFileInfo>(json);
    }

    public async Task<ProgramFileContentResult> GetProgramFileContentAsync(string fileName, CancellationToken cancellationToken)
    {
        string safeFileName = SafeProgramFileName(fileName);
        using HttpResponseMessage response = await _httpClient.GetAsync(new Uri(BaseUri, $"remote/program/file?filename={Uri.EscapeDataString(safeFileName)}&content=1"), cancellationToken).ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        string json = await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
        return RemoteProtocolJson.Deserialize<ProgramFileContentResult>(json);
    }

    public async Task<ProgramDeleteResult> DeleteProgramFileAsync(string fileName, CancellationToken cancellationToken)
    {
        string safeFileName = SafeProgramFileName(fileName);
        using HttpRequestMessage request = new(HttpMethod.Delete, new Uri(BaseUri, $"remote/program/file?filename={Uri.EscapeDataString(safeFileName)}"));
        using HttpResponseMessage response = await _httpClient.SendAsync(request, cancellationToken).ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        string json = await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
        return RemoteProtocolJson.Deserialize<ProgramDeleteResult>(json);
    }

    public async Task<ProgramUploadResult> UploadProgramAsync(string fileName, Stream content, long contentLength, string sha256, bool overwrite, CancellationToken cancellationToken)
    {
        string safeFileName = SafeProgramFileName(fileName);

        Uri requestUri = new(BaseUri, $"remote/program/upload?filename={Uri.EscapeDataString(safeFileName)}&overwrite={(overwrite ? "1" : "0")}");
        using HttpRequestMessage request = new(HttpMethod.Post, requestUri);
        using StreamContent streamContent = new(content);
        streamContent.Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");
        streamContent.Headers.ContentLength = contentLength;
        streamContent.Headers.TryAddWithoutValidation("X-8ax-File-Sha256", sha256);
        request.Content = streamContent;

        using HttpResponseMessage response = await _httpClient.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, cancellationToken).ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        string json = await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
        return RemoteProtocolJson.Deserialize<ProgramUploadResult>(json);
    }

    public async Task<OtaUpgradeResult> RequestOtaUpgradeAsync(CancellationToken cancellationToken)
    {
        OtaUpgradeRequest payload = new(
            "re.v3.remote_ota_upgrade_request.v1",
            "winremote_bottom_button",
            DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
            "dna_private_first_no_public_when_private_present");
        using HttpRequestMessage request = new(HttpMethod.Post, new Uri(BaseUri, "remote/ota/upgrade"));
        request.Content = new StringContent(RemoteProtocolJson.Serialize(payload), Encoding.UTF8, "application/json");
        using HttpResponseMessage response = await _httpClient.SendAsync(request, cancellationToken).ConfigureAwait(false);
        string json = await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
        if (!response.IsSuccessStatusCode)
        {
            throw new InvalidOperationException($"OTA upgrade request failed: {(int)response.StatusCode} {response.ReasonPhrase} {json}".Trim());
        }

        return RemoteProtocolJson.Deserialize<OtaUpgradeResult>(json);
    }

    private static string SafeProgramFileName(string fileName)
    {
        string safeFileName = Path.GetFileName(fileName);
        if (String.IsNullOrWhiteSpace(safeFileName) || !String.Equals(fileName, safeFileName, StringComparison.Ordinal))
        {
            throw new ArgumentException("Program file name must be a basename.", nameof(fileName));
        }

        return safeFileName;
    }

    public async IAsyncEnumerable<RemoteFramePacket> ReadFrameStreamAsync([EnumeratorCancellation] CancellationToken cancellationToken)
    {
        using ClientWebSocket socket = new();
        socket.Options.Proxy = null;
        await ConnectWebSocketAsync(socket, ToWebSocketUri(new Uri(BaseUri, "remote/stream")), StreamConnectTimeout, cancellationToken).ConfigureAwait(false);

        while (!cancellationToken.IsCancellationRequested && socket.State == WebSocketState.Open)
        {
            WebSocketMessage metadataMessage = await ReceiveMessageAsync(socket, cancellationToken).ConfigureAwait(false);
            if (metadataMessage.Type == WebSocketMessageType.Close)
            {
                yield break;
            }

            if (metadataMessage.Type != WebSocketMessageType.Text)
            {
                throw new InvalidOperationException("Expected WebSocket text metadata before frame payload.");
            }

            FrameMetadata metadata = RemoteProtocolJson.Deserialize<FrameMetadata>(Encoding.UTF8.GetString(metadataMessage.Payload));
            if (metadata.Type is not (RemoteMessageTypes.FullFrame or RemoteMessageTypes.DirtyRects))
            {
                continue;
            }

            WebSocketMessage payloadMessage = await ReceiveMessageAsync(socket, cancellationToken).ConfigureAwait(false);
            if (payloadMessage.Type == WebSocketMessageType.Close)
            {
                yield break;
            }

            if (payloadMessage.Type != WebSocketMessageType.Binary)
            {
                throw new InvalidOperationException("Expected WebSocket binary frame payload after metadata.");
            }

            yield return new RemoteFramePacket(metadata, payloadMessage.Payload);
        }
    }

    public async Task<PointerAckMessage> OpenInputAsync(string sessionId, CancellationToken cancellationToken)
    {
        await _inputGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (_inputSocket is { State: WebSocketState.Open } && String.Equals(_inputSessionId, sessionId, StringComparison.Ordinal))
            {
                return new PointerAckMessage(RemoteMessageTypes.ControlGrant, sessionId, 0, "control", true, UnixTimeMs(), null);
            }

            _inputSocket?.Dispose();
            ClientWebSocket socket = new();
            socket.Options.Proxy = null;
            await ConnectWebSocketAsync(socket, ToWebSocketUri(new Uri(BaseUri, "remote/input")), InputConnectTimeout, cancellationToken).ConfigureAwait(false);
            _inputSocket = socket;
            _inputSessionId = sessionId;

            ControlRequestMessage request = new(RemoteMessageTypes.ControlRequest, sessionId, "8ax-win", UnixTimeMs());
            PointerAckMessage response = await SendInputMessageAndReadAckLockedAsync(request, cancellationToken).ConfigureAwait(false);
            if (response.Type != RemoteMessageTypes.ControlGrant || !response.Accepted)
            {
                throw new InvalidOperationException($"Remote input not granted: {response.Reason ?? response.Type}");
            }

            return response;
        }
        finally
        {
            _inputGate.Release();
        }
    }

    public async Task<PointerAckMessage> SendPointerEventAsync(PointerEventMessage message, CancellationToken cancellationToken)
    {
        await _inputGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (_inputSocket is not { State: WebSocketState.Open })
            {
                throw new InvalidOperationException("Remote input WebSocket is not connected.");
            }

            PointerAckMessage response = await SendInputMessageAndReadAckLockedAsync(message, cancellationToken).ConfigureAwait(false);
            if (response.Type == RemoteMessageTypes.PointerReject || !response.Accepted)
            {
                throw new InvalidOperationException($"Remote input rejected: {response.Reason ?? response.Type}");
            }

            if (response.Sequence != message.Sequence)
            {
                throw new InvalidOperationException($"Remote input ACK sequence mismatch: {response.Sequence} != {message.Sequence}");
            }

            return response;
        }
        catch
        {
            ResetInputSocketLocked();
            throw;
        }
        finally
        {
            _inputGate.Release();
        }
    }

    public void Dispose()
    {
        ResetInputSocketLocked();
        _inputGate.Dispose();
        _httpClient.Dispose();
    }

    private void ResetInputSocketLocked()
    {
        _inputSocket?.Dispose();
        _inputSocket = null;
        _inputSessionId = null;
    }

    private static Uri ToWebSocketUri(Uri uri)
    {
        UriBuilder builder = new(uri);
        builder.Scheme = uri.Scheme.Equals("https", StringComparison.OrdinalIgnoreCase) ? "wss" : "ws";
        return builder.Uri;
    }

    private static async Task ConnectWebSocketAsync(ClientWebSocket socket, Uri uri, TimeSpan timeout, CancellationToken cancellationToken)
    {
        using CancellationTokenSource timeoutToken = new(timeout);
        using CancellationTokenSource linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, timeoutToken.Token);
        try
        {
            await socket.ConnectAsync(uri, linked.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested && timeoutToken.IsCancellationRequested)
        {
            throw new TimeoutException($"WebSocket connect timed out after {timeout.TotalSeconds:0.#}s: {uri}");
        }
    }

    private async Task<PointerAckMessage> SendInputMessageAndReadAckLockedAsync<T>(T message, CancellationToken cancellationToken)
    {
        if (_inputSocket is not { State: WebSocketState.Open } socket)
        {
            throw new InvalidOperationException("Remote input WebSocket is not connected.");
        }

        byte[] payload = Encoding.UTF8.GetBytes(RemoteProtocolJson.Serialize(message));
        await socket.SendAsync(new ArraySegment<byte>(payload), WebSocketMessageType.Text, true, cancellationToken).ConfigureAwait(false);

        WebSocketMessage response = await ReceiveMessageAsync(socket, cancellationToken).ConfigureAwait(false);
        if (response.Type == WebSocketMessageType.Close)
        {
            throw new InvalidOperationException("Remote input WebSocket closed.");
        }
        if (response.Type != WebSocketMessageType.Text)
        {
            throw new InvalidOperationException("Expected remote input ACK text message.");
        }

        return RemoteProtocolJson.Deserialize<PointerAckMessage>(Encoding.UTF8.GetString(response.Payload));
    }

    private static long UnixTimeMs() => DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

    private static async Task<ClientTimeStamp> ResolveClientTimeAsync(CancellationToken cancellationToken)
    {
        DateTimeOffset now = DateTimeOffset.UtcNow;
        if (s_networkTimeOffset is { } offset && now - s_networkTimeOffsetUpdatedUtc < NetworkTimeCacheDuration)
        {
            return new ClientTimeStamp((now + offset).ToUnixTimeMilliseconds(), ClientTimeNetworkSource);
        }

        if (now - s_lastNetworkTimeFailureUtc < NetworkTimeRetryDelay)
        {
            return new ClientTimeStamp(now.ToUnixTimeMilliseconds(), ClientTimeLocalSource);
        }

        await NetworkTimeGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            now = DateTimeOffset.UtcNow;
            if (s_networkTimeOffset is { } refreshedOffset && now - s_networkTimeOffsetUpdatedUtc < NetworkTimeCacheDuration)
            {
                return new ClientTimeStamp((now + refreshedOffset).ToUnixTimeMilliseconds(), ClientTimeNetworkSource);
            }

            foreach (Uri uri in NetworkTimeUris)
            {
                if (await TryReadNetworkTimeOffsetAsync(uri, cancellationToken).ConfigureAwait(false) is { } networkOffset)
                {
                    s_networkTimeOffset = networkOffset;
                    s_networkTimeOffsetUpdatedUtc = DateTimeOffset.UtcNow;
                    return new ClientTimeStamp((DateTimeOffset.UtcNow + networkOffset).ToUnixTimeMilliseconds(), ClientTimeNetworkSource);
                }
            }

            s_lastNetworkTimeFailureUtc = DateTimeOffset.UtcNow;
            return new ClientTimeStamp(DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(), ClientTimeLocalSource);
        }
        finally
        {
            NetworkTimeGate.Release();
        }
    }

    private static async Task<TimeSpan?> TryReadNetworkTimeOffsetAsync(Uri uri, CancellationToken cancellationToken)
    {
        DateTimeOffset before = DateTimeOffset.UtcNow;
        try
        {
            using CancellationTokenSource timeout = new(NetworkTimeProbeTimeout);
            using CancellationTokenSource linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, timeout.Token);
            using HttpRequestMessage request = new(HttpMethod.Head, uri);
            using HttpResponseMessage response = await NetworkTimeHttpClient.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, linked.Token).ConfigureAwait(false);
            DateTimeOffset after = DateTimeOffset.UtcNow;
            DateTimeOffset? networkDate = response.Headers.Date;
            if (networkDate is null)
            {
                return null;
            }

            TimeSpan roundTrip = after - before;
            DateTimeOffset midpoint = before + TimeSpan.FromTicks(roundTrip.Ticks / 2);
            return networkDate.Value - midpoint;
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch
        {
            return null;
        }
    }

    private static async Task<WebSocketMessage> ReceiveMessageAsync(ClientWebSocket socket, CancellationToken cancellationToken)
    {
        byte[] buffer = new byte[64 * 1024];
        using MemoryStream stream = new();

        while (true)
        {
            WebSocketReceiveResult result = await socket.ReceiveAsync(buffer, cancellationToken).ConfigureAwait(false);
            if (result.MessageType == WebSocketMessageType.Close)
            {
                return new WebSocketMessage(WebSocketMessageType.Close, Array.Empty<byte>());
            }

            stream.Write(buffer, 0, result.Count);
            if (result.EndOfMessage)
            {
                return new WebSocketMessage(result.MessageType, stream.ToArray());
            }
        }
    }

    private sealed record WebSocketMessage(WebSocketMessageType Type, byte[] Payload);
    private sealed record ClientTimeStamp(long UnixTimeMs, string Source);
}
