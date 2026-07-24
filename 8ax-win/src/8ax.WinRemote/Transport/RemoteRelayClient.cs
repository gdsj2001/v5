using System.IO;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.WebSockets;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using EightAxis.WinRemote.Config;
using EightAxis.WinRemote.Protocol;

namespace EightAxis.WinRemote.Transport;

public sealed class RemoteRelayClient : IRemoteTransport, IDisposable
{
    private const long MaxProgramFileBytes = 64L * 1024 * 1024;
    private static readonly TimeSpan StreamConnectTimeout = TimeSpan.FromSeconds(3);
    private static readonly TimeSpan InputConnectTimeout = TimeSpan.FromSeconds(2);
    private const string ClientTimeUnixMsHeader = "X-8ax-Client-Time-Unix-Ms";
    private const string ClientTimeSourceHeader = "X-8ax-Client-Time-Source";

    private readonly HttpClient _httpClient;
    private readonly RelayAuthenticationSession _authentication;
    private readonly SemaphoreSlim _inputGate = new(1, 1);
    private ClientWebSocket? _inputSocket;
    private string? _inputSessionId;

    public RemoteRelayClient(RelaySecurityProfile security)
    {
        ArgumentNullException.ThrowIfNull(security);
        BaseUri = security.RelayBaseUri;
        _httpClient = new HttpClient(security.CreateHttpClientHandler(), disposeHandler: true);
        _authentication = new RelayAuthenticationSession(security, _httpClient);
    }

    public Uri BaseUri { get; }

    public async Task<RemoteInfoMessage> GetInfoAsync(CancellationToken cancellationToken)
    {
        ClientNetworkTime.ClientTimeStamp clientTime = await ClientNetworkTime.ResolveAsync(cancellationToken).ConfigureAwait(false);
        using HttpResponseMessage response = await _authentication.SendAsync(() =>
        {
            HttpRequestMessage request = new(HttpMethod.Get, new Uri(BaseUri, "remote/info"));
            request.Headers.TryAddWithoutValidation(ClientTimeUnixMsHeader, clientTime.UnixTimeMs.ToString(System.Globalization.CultureInfo.InvariantCulture));
            request.Headers.TryAddWithoutValidation(ClientTimeSourceHeader, clientTime.Source);
            return request;
        }, HttpCompletionOption.ResponseContentRead, cancellationToken).ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        string json = await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
        return RemoteProtocolJson.Deserialize<RemoteInfoMessage>(json);
    }

    public async Task<RemoteFramePacket> GetFullFrameAsync(CancellationToken cancellationToken)
    {
        using HttpResponseMessage response = await _authentication.SendAsync(
            () => new HttpRequestMessage(HttpMethod.Get, new Uri(BaseUri, "remote/frame/full")),
            HttpCompletionOption.ResponseContentRead,
            cancellationToken).ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        byte[] envelope = await response.Content.ReadAsByteArrayAsync(cancellationToken).ConfigureAwait(false);
        return RemoteProtocolJson.DecodeFrameEnvelope(envelope);
    }

    public async Task<string> GetDiagnosticsJsonAsync(CancellationToken cancellationToken)
    {
        using HttpResponseMessage response = await _authentication.SendAsync(
            () => new HttpRequestMessage(HttpMethod.Get, new Uri(BaseUri, "remote/diagnostics")),
            HttpCompletionOption.ResponseContentRead,
            cancellationToken).ConfigureAwait(false);
        return await ReadRelayJsonAsync(response, "读取板端日志", cancellationToken).ConfigureAwait(false);
    }

    public async Task<ProgramListResult> GetProgramListAsync(CancellationToken cancellationToken)
    {
        using HttpResponseMessage response = await _authentication.SendAsync(
            () => new HttpRequestMessage(HttpMethod.Get, new Uri(BaseUri, "remote/program/list")),
            HttpCompletionOption.ResponseContentRead,
            cancellationToken).ConfigureAwait(false);
        string json = await ReadRelayJsonAsync(response, "读取板端 G-code 列表", cancellationToken).ConfigureAwait(false);
        return RemoteProtocolJson.Deserialize<ProgramListResult>(json);
    }

    public async Task<ProgramFileInfo> GetProgramFileInfoAsync(string fileName, CancellationToken cancellationToken)
    {
        string safeFileName = SafeProgramFileName(fileName);
        using HttpResponseMessage response = await _authentication.SendAsync(
            () => new HttpRequestMessage(HttpMethod.Get, new Uri(BaseUri, $"remote/program/file?filename={Uri.EscapeDataString(safeFileName)}")),
            HttpCompletionOption.ResponseContentRead,
            cancellationToken).ConfigureAwait(false);
        string json = await ReadRelayJsonAsync(response, "检查板端 G-code 文件", cancellationToken).ConfigureAwait(false);
        return RemoteProtocolJson.Deserialize<ProgramFileInfo>(json);
    }

    public async Task<ProgramFileContentResult> GetProgramFileContentAsync(string fileName, CancellationToken cancellationToken)
    {
        string safeFileName = SafeProgramFileName(fileName);
        using HttpResponseMessage response = await _authentication.SendAsync(
            () => new HttpRequestMessage(HttpMethod.Get, new Uri(BaseUri, $"remote/program/file?filename={Uri.EscapeDataString(safeFileName)}&content=1")),
            HttpCompletionOption.ResponseContentRead,
            cancellationToken).ConfigureAwait(false);
        string json = await ReadRelayJsonAsync(response, "读取板端 G-code 文件", cancellationToken).ConfigureAwait(false);
        return RemoteProtocolJson.Deserialize<ProgramFileContentResult>(json);
    }

    public async Task<ProgramDeleteResult> DeleteProgramFileAsync(string fileName, CancellationToken cancellationToken)
    {
        string safeFileName = SafeProgramFileName(fileName);
        using HttpResponseMessage response = await _authentication.SendAsync(
            () => new HttpRequestMessage(HttpMethod.Delete, new Uri(BaseUri, $"remote/program/file?filename={Uri.EscapeDataString(safeFileName)}")),
            HttpCompletionOption.ResponseContentRead,
            cancellationToken).ConfigureAwait(false);
        string json = await ReadRelayJsonAsync(response, "删除板端 G-code 文件", cancellationToken).ConfigureAwait(false);
        return RemoteProtocolJson.Deserialize<ProgramDeleteResult>(json);
    }

    public async Task<ProgramUploadResult> UploadProgramAsync(string fileName, Stream content, long contentLength, string sha256, bool overwrite, CancellationToken cancellationToken)
    {
        string safeFileName = SafeProgramFileName(fileName);
        if (contentLength <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(contentLength), "G-code 文件不能为空。");
        }
        if (contentLength > MaxProgramFileBytes)
        {
            throw new ArgumentOutOfRangeException(nameof(contentLength), "G-code 文件超过板端 64 MiB 上限。");
        }
        if (sha256.Length != 64 || sha256.Any(ch => !Uri.IsHexDigit(ch)))
        {
            throw new ArgumentException("G-code 文件 SHA256 无效。", nameof(sha256));
        }

        byte[] uploadBytes = new byte[checked((int)contentLength)];
        int readOffset = 0;
        while (readOffset < uploadBytes.Length)
        {
            int read = await content.ReadAsync(
                uploadBytes.AsMemory(readOffset, uploadBytes.Length - readOffset),
                cancellationToken).ConfigureAwait(false);
            if (read == 0)
            {
                throw new InvalidOperationException("G-code upload stream ended before the declared length.");
            }
            readOffset += read;
        }
        Uri requestUri = new(BaseUri, $"remote/program/upload?filename={Uri.EscapeDataString(safeFileName)}&overwrite={(overwrite ? "1" : "0")}");
        using HttpResponseMessage response = await _authentication.SendAsync(() =>
        {
            HttpRequestMessage request = new(HttpMethod.Post, requestUri);
            ByteArrayContent requestContent = new(uploadBytes);
            requestContent.Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");
            requestContent.Headers.ContentLength = contentLength;
            requestContent.Headers.TryAddWithoutValidation("X-8ax-File-Sha256", sha256);
            request.Content = requestContent;
            return request;
        }, HttpCompletionOption.ResponseHeadersRead, cancellationToken).ConfigureAwait(false);
        string json = await ReadRelayJsonAsync(response, "上传 G-code 到板端", cancellationToken).ConfigureAwait(false);
        return RemoteProtocolJson.Deserialize<ProgramUploadResult>(json);
    }

    public async Task<OtaUpgradeResult> RequestOtaUpgradeAsync(CancellationToken cancellationToken)
    {
        OtaUpgradeRequest payload = new(
            "re.v3.remote_ota_upgrade_request.v1",
            "winremote_bottom_button",
            DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
            "dna_private_first_no_public_when_private_present");
        string requestJson = RemoteProtocolJson.Serialize(payload);
        using HttpResponseMessage response = await _authentication.SendAsync(() =>
        {
            HttpRequestMessage request = new(HttpMethod.Post, new Uri(BaseUri, "remote/ota/upgrade"));
            request.Content = new StringContent(requestJson, Encoding.UTF8, "application/json");
            return request;
        }, HttpCompletionOption.ResponseContentRead, cancellationToken).ConfigureAwait(false);
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
        if (String.IsNullOrWhiteSpace(safeFileName)
            || !String.Equals(fileName, fileName.Trim(), StringComparison.Ordinal)
            || fileName.Contains('/')
            || fileName.Contains('\\')
            || !String.Equals(fileName, safeFileName, StringComparison.Ordinal))
        {
            throw new ArgumentException("G-code 文件名必须是 basename，不能包含路径或首尾空格。", nameof(fileName));
        }

        string extension = Path.GetExtension(safeFileName).ToLowerInvariant();
        if (extension is not (".ngc" or ".nc" or ".tap" or ".gcode"))
        {
            throw new ArgumentException("只允许 .ngc、.nc、.tap、.gcode 文件。", nameof(fileName));
        }

        return safeFileName;
    }

    private static async Task<string> ReadRelayJsonAsync(
        HttpResponseMessage response,
        string operation,
        CancellationToken cancellationToken)
    {
        string json = await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
        if (response.IsSuccessStatusCode)
        {
            return json;
        }

        string detail = ReadRelayErrorDetail(json);
        string reason = String.IsNullOrWhiteSpace(response.ReasonPhrase) ? String.Empty : $" {response.ReasonPhrase}";
        throw new InvalidOperationException($"{operation}失败：HTTP {(int)response.StatusCode}{reason}；{detail}");
    }

    private static string ReadRelayErrorDetail(string body)
    {
        string? code = null;
        string? message = null;
        try
        {
            using JsonDocument document = JsonDocument.Parse(body);
            JsonElement root = document.RootElement;
            if (root.ValueKind == JsonValueKind.Object)
            {
                if (root.TryGetProperty("error", out JsonElement errorElement) && errorElement.ValueKind == JsonValueKind.String)
                {
                    code = errorElement.GetString();
                }
                if (root.TryGetProperty("message", out JsonElement messageElement) && messageElement.ValueKind == JsonValueKind.String)
                {
                    message = messageElement.GetString();
                }
            }
        }
        catch (JsonException)
        {
        }

        if (String.Equals(code, "unsupported_remote_path", StringComparison.Ordinal))
        {
            return "当前板端未安装 G-code 文件接口，请更新板端 remote_ui_relay。";
        }
        if (!String.IsNullOrWhiteSpace(message))
        {
            return String.IsNullOrWhiteSpace(code) ? message : $"{message}（{code}）";
        }
        if (!String.IsNullOrWhiteSpace(code))
        {
            return code;
        }

        string compact = body.Replace('\r', ' ').Replace('\n', ' ').Trim();
        if (compact.Length > 240)
        {
            compact = compact[..240] + "...";
        }
        return String.IsNullOrWhiteSpace(compact) ? "板端没有返回错误详情。" : compact;
    }

    public async IAsyncEnumerable<RemoteFramePacket> ReadFrameStreamAsync([EnumeratorCancellation] CancellationToken cancellationToken)
    {
        using ClientWebSocket socket = await _authentication.ConnectWebSocketAsync(
            ToWebSocketUri(new Uri(BaseUri, "remote/stream")),
            StreamConnectTimeout,
            cancellationToken).ConfigureAwait(false);

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
            ClientWebSocket socket = await _authentication.ConnectWebSocketAsync(
                ToWebSocketUri(new Uri(BaseUri, "remote/input")),
                InputConnectTimeout,
                cancellationToken).ConfigureAwait(false);
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
        _authentication.Dispose();
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
        if (uri.Scheme != Uri.UriSchemeHttps)
        {
            throw new InvalidOperationException("WinRemote relay transport requires HTTPS/WSS.");
        }
        UriBuilder builder = new(uri);
        builder.Scheme = "wss";
        return builder.Uri;
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
}
