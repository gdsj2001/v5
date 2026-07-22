using System.Diagnostics;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using EightAxis.WinRemote.Config;

namespace EightAxis.WinRemote.Transport;

internal sealed class RelayAuthenticationSession : IDisposable
{
    private const int MaximumSessionSeconds = 600;
    private const int SessionExpiryMarginSeconds = 15;
    private static readonly Regex Base64UrlPattern = new("^[A-Za-z0-9_-]+$", RegexOptions.CultureInvariant);

    private readonly RelaySecurityProfile _profile;
    private readonly HttpClient _httpClient;
    private readonly SemaphoreSlim _sessionGate = new(1, 1);
    private string? _sessionToken;
    private long _sessionExpiresAtTimestamp;

    internal RelayAuthenticationSession(RelaySecurityProfile profile, HttpClient httpClient)
    {
        _profile = profile;
        _httpClient = httpClient;
    }

    internal async Task<HttpResponseMessage> SendAsync(
        Func<HttpRequestMessage> requestFactory,
        HttpCompletionOption completionOption,
        CancellationToken cancellationToken)
    {
        for (int attempt = 0; attempt < 2; attempt++)
        {
            string authorization = await GetAuthorizationAsync(cancellationToken).ConfigureAwait(false);
            using HttpRequestMessage request = requestFactory();
            request.Headers.Authorization = AuthenticationHeaderValue.Parse(authorization);
            HttpResponseMessage response = await _httpClient.SendAsync(
                request,
                completionOption,
                cancellationToken).ConfigureAwait(false);
            if (response.StatusCode != HttpStatusCode.Unauthorized || attempt != 0)
            {
                return response;
            }
            response.Dispose();
            Invalidate();
        }
        throw new InvalidOperationException("Relay authentication retry exhausted.");
    }

    internal async Task<ClientWebSocket> ConnectWebSocketAsync(
        Uri uri,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        if (uri.Scheme != "wss")
        {
            throw new InvalidOperationException("WinRemote relay WebSocket must use WSS.");
        }
        for (int attempt = 0; attempt < 2; attempt++)
        {
            string authorization = await GetAuthorizationAsync(cancellationToken).ConfigureAwait(false);
            ClientWebSocket socket = new();
            _profile.ConfigureWebSocket(socket.Options);
            socket.Options.SetRequestHeader("Authorization", authorization);
            using CancellationTokenSource timeoutToken = new(timeout);
            using CancellationTokenSource linked = CancellationTokenSource.CreateLinkedTokenSource(
                cancellationToken,
                timeoutToken.Token);
            try
            {
                await socket.ConnectAsync(uri, linked.Token).ConfigureAwait(false);
                return socket;
            }
            catch (OperationCanceledException) when (
                !cancellationToken.IsCancellationRequested && timeoutToken.IsCancellationRequested)
            {
                socket.Dispose();
                throw new TimeoutException(
                    $"Authenticated WSS connect timed out after {timeout.TotalSeconds:0.#}s: {uri}");
            }
            catch (WebSocketException) when (attempt == 0)
            {
                socket.Dispose();
                Invalidate();
            }
            catch
            {
                socket.Dispose();
                throw;
            }
        }
        throw new InvalidOperationException("Relay WebSocket authentication retry exhausted.");
    }

    internal void Invalidate()
    {
        _sessionToken = null;
        Interlocked.Exchange(ref _sessionExpiresAtTimestamp, 0);
    }

    public void Dispose()
    {
        Invalidate();
        _sessionGate.Dispose();
    }

    private async Task<string> GetAuthorizationAsync(CancellationToken cancellationToken)
    {
        string? current = Volatile.Read(ref _sessionToken);
        if (!String.IsNullOrEmpty(current)
            && Stopwatch.GetTimestamp() < Interlocked.Read(ref _sessionExpiresAtTimestamp))
        {
            return RelaySecurityProfile.SessionScheme + " " + current;
        }

        await _sessionGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            current = _sessionToken;
            if (!String.IsNullOrEmpty(current)
                && Stopwatch.GetTimestamp() < _sessionExpiresAtTimestamp)
            {
                return RelaySecurityProfile.SessionScheme + " " + current;
            }
            SessionResult result = await AuthenticateAsync(cancellationToken).ConfigureAwait(false);
            _sessionToken = result.Token;
            long lifetimeTicks = checked((long)(result.ExpiresInSeconds - SessionExpiryMarginSeconds)
                * Stopwatch.Frequency);
            _sessionExpiresAtTimestamp = checked(Stopwatch.GetTimestamp() + lifetimeTicks);
            return RelaySecurityProfile.SessionScheme + " " + result.Token;
        }
        finally
        {
            _sessionGate.Release();
        }
    }

    private async Task<SessionResult> AuthenticateAsync(CancellationToken cancellationToken)
    {
        Uri challengeUri = new(
            _profile.RelayBaseUri,
            "remote/auth/challenge?client_id=" + Uri.EscapeDataString(_profile.ClientId));
        using HttpRequestMessage challengeRequest = new(HttpMethod.Get, challengeUri);
        using HttpResponseMessage challengeResponse = await _httpClient.SendAsync(
            challengeRequest,
            HttpCompletionOption.ResponseContentRead,
            cancellationToken).ConfigureAwait(false);
        string challengeJson = await challengeResponse.Content.ReadAsStringAsync(cancellationToken)
            .ConfigureAwait(false);
        if (!challengeResponse.IsSuccessStatusCode)
        {
            throw new InvalidOperationException(
                $"Relay challenge failed with HTTP {(int)challengeResponse.StatusCode}.");
        }
        Challenge challenge = ParseChallenge(challengeJson);
        string[] requestedScopes = _profile.Scopes.Order(StringComparer.Ordinal).ToArray();
        string mac = _profile.ComputeAuthenticationMac(
            challenge.ChallengeId,
            challenge.Nonce,
            requestedScopes);
        string requestJson = JsonSerializer.Serialize(new Dictionary<string, object>
        {
            ["schema"] = "v5.remote_auth_session_request.v1",
            ["protocol"] = RelaySecurityProfile.AuthenticationProtocol,
            ["client_id"] = _profile.ClientId,
            ["challenge_id"] = challenge.ChallengeId,
            ["nonce"] = challenge.Nonce,
            ["requested_scopes"] = requestedScopes,
            ["mac"] = mac,
        });
        using HttpRequestMessage sessionRequest = new(
            HttpMethod.Post,
            new Uri(_profile.RelayBaseUri, "remote/auth/session"))
        {
            Content = new StringContent(requestJson, Encoding.UTF8, "application/json"),
        };
        using HttpResponseMessage sessionResponse = await _httpClient.SendAsync(
            sessionRequest,
            HttpCompletionOption.ResponseContentRead,
            cancellationToken).ConfigureAwait(false);
        string sessionJson = await sessionResponse.Content.ReadAsStringAsync(cancellationToken)
            .ConfigureAwait(false);
        if (!sessionResponse.IsSuccessStatusCode)
        {
            throw new InvalidOperationException(
                $"Relay session authentication failed with HTTP {(int)sessionResponse.StatusCode}.");
        }
        return ParseSession(sessionJson, requestedScopes);
    }

    private Challenge ParseChallenge(string json)
    {
        Dictionary<string, JsonElement> fields = ParseStrictObject(json, new HashSet<string>(StringComparer.Ordinal)
        {
            "schema",
            "protocol",
            "device_id",
            "challenge_id",
            "nonce",
            "expires_in_seconds",
        });
        if (ReadString(fields, "schema") != "v5.remote_auth_challenge.v1"
            || ReadString(fields, "protocol") != RelaySecurityProfile.AuthenticationProtocol
            || ReadString(fields, "device_id") != _profile.DeviceId)
        {
            throw new InvalidOperationException("Relay challenge identity is invalid.");
        }
        string challengeId = ReadBase64Url(fields, "challenge_id");
        string nonce = ReadBase64Url(fields, "nonce");
        int expires = ReadInt32(fields, "expires_in_seconds");
        if (expires is <= 0 or > 30)
        {
            throw new InvalidOperationException("Relay challenge lifetime is invalid.");
        }
        return new Challenge(challengeId, nonce);
    }

    private SessionResult ParseSession(string json, IReadOnlyCollection<string> requestedScopes)
    {
        Dictionary<string, JsonElement> fields = ParseStrictObject(json, new HashSet<string>(StringComparer.Ordinal)
        {
            "schema",
            "protocol",
            "device_id",
            "client_id",
            "session_token",
            "scopes",
            "expires_in_seconds",
        });
        if (ReadString(fields, "schema") != "v5.remote_auth_session.v1"
            || ReadString(fields, "protocol") != RelaySecurityProfile.AuthenticationProtocol
            || ReadString(fields, "device_id") != _profile.DeviceId
            || ReadString(fields, "client_id") != _profile.ClientId)
        {
            throw new InvalidOperationException("Relay session identity is invalid.");
        }
        string token = ReadBase64Url(fields, "session_token");
        int expires = ReadInt32(fields, "expires_in_seconds");
        if (expires <= SessionExpiryMarginSeconds || expires > MaximumSessionSeconds)
        {
            throw new InvalidOperationException("Relay session lifetime is invalid.");
        }
        JsonElement scopesElement = fields["scopes"];
        if (scopesElement.ValueKind != JsonValueKind.Array)
        {
            throw new InvalidOperationException("Relay session scopes are invalid.");
        }
        string[] actualScopes = scopesElement.EnumerateArray().Select(element =>
            element.ValueKind == JsonValueKind.String
                ? element.GetString() ?? String.Empty
                : String.Empty).Order(StringComparer.Ordinal).ToArray();
        if (!actualScopes.SequenceEqual(requestedScopes.Order(StringComparer.Ordinal), StringComparer.Ordinal))
        {
            throw new InvalidOperationException("Relay session scopes do not match the authenticated request.");
        }
        return new SessionResult(token, expires);
    }

    private static Dictionary<string, JsonElement> ParseStrictObject(
        string json,
        IReadOnlySet<string> expectedFields)
    {
        using JsonDocument document = JsonDocument.Parse(json, new JsonDocumentOptions
        {
            AllowTrailingCommas = false,
            CommentHandling = JsonCommentHandling.Disallow,
            MaxDepth = 8,
        });
        if (document.RootElement.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidOperationException("Relay authentication response must be an object.");
        }
        Dictionary<string, JsonElement> fields = new(StringComparer.Ordinal);
        foreach (JsonProperty property in document.RootElement.EnumerateObject())
        {
            if (!fields.TryAdd(property.Name, property.Value.Clone()))
            {
                throw new InvalidOperationException("Relay authentication response contains duplicate fields.");
            }
        }
        if (fields.Count != expectedFields.Count || fields.Keys.Any(key => !expectedFields.Contains(key)))
        {
            throw new InvalidOperationException("Relay authentication response fields are invalid.");
        }
        return fields;
    }

    private static string ReadString(IReadOnlyDictionary<string, JsonElement> fields, string name)
    {
        JsonElement value = fields[name];
        if (value.ValueKind != JsonValueKind.String || String.IsNullOrWhiteSpace(value.GetString()))
        {
            throw new InvalidOperationException($"Relay authentication field {name} is invalid.");
        }
        return value.GetString()!;
    }

    private static string ReadBase64Url(IReadOnlyDictionary<string, JsonElement> fields, string name)
    {
        string value = ReadString(fields, name);
        if (!Base64UrlPattern.IsMatch(value))
        {
            throw new InvalidOperationException($"Relay authentication field {name} is not base64url.");
        }
        return value;
    }

    private static int ReadInt32(IReadOnlyDictionary<string, JsonElement> fields, string name)
    {
        JsonElement value = fields[name];
        if (value.ValueKind != JsonValueKind.Number || !value.TryGetInt32(out int parsed))
        {
            throw new InvalidOperationException($"Relay authentication field {name} is invalid.");
        }
        return parsed;
    }

    private sealed record Challenge(string ChallengeId, string Nonce);
    private sealed record SessionResult(string Token, int ExpiresInSeconds);
}
