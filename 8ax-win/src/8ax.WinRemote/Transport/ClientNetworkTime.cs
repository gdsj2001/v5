using System.Net.Http;

namespace EightAxis.WinRemote.Transport;

internal static class ClientNetworkTime
{
    internal const string NetworkSource = "winremote-network-time";
    internal const string LocalSource = "winremote-local-time";

    private static readonly TimeSpan ProbeTimeout = TimeSpan.FromMilliseconds(700);
    private static readonly TimeSpan CacheDuration = TimeSpan.FromMinutes(5);
    private static readonly TimeSpan RetryDelay = TimeSpan.FromMinutes(1);
    private static readonly Uri[] ProbeUris =
    [
        new("https://license.cjwsjzyy.xyz/8ax-winremote/win-x64/manifest.json"),
        new("https://license.3dtouch.top/8ax-winremote/win-x64/manifest.json"),
    ];
    private static readonly HttpClient HttpClient = new();
    private static readonly SemaphoreSlim Gate = new(1, 1);
    private static TimeSpan? s_offset;
    private static DateTimeOffset s_offsetUpdatedUtc = DateTimeOffset.MinValue;
    private static DateTimeOffset s_lastFailureUtc = DateTimeOffset.MinValue;

    internal static async Task<ClientTimeStamp> ResolveAsync(CancellationToken cancellationToken)
    {
        DateTimeOffset now = DateTimeOffset.UtcNow;
        if (s_offset is { } offset && now - s_offsetUpdatedUtc < CacheDuration)
        {
            return new ClientTimeStamp((now + offset).ToUnixTimeMilliseconds(), NetworkSource);
        }
        if (now - s_lastFailureUtc < RetryDelay)
        {
            return new ClientTimeStamp(now.ToUnixTimeMilliseconds(), LocalSource);
        }
        await Gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            now = DateTimeOffset.UtcNow;
            if (s_offset is { } refreshedOffset && now - s_offsetUpdatedUtc < CacheDuration)
            {
                return new ClientTimeStamp((now + refreshedOffset).ToUnixTimeMilliseconds(), NetworkSource);
            }
            foreach (Uri uri in ProbeUris)
            {
                if (await TryReadOffsetAsync(uri, cancellationToken).ConfigureAwait(false) is { } networkOffset)
                {
                    s_offset = networkOffset;
                    s_offsetUpdatedUtc = DateTimeOffset.UtcNow;
                    return new ClientTimeStamp(
                        (DateTimeOffset.UtcNow + networkOffset).ToUnixTimeMilliseconds(),
                        NetworkSource);
                }
            }
            s_lastFailureUtc = DateTimeOffset.UtcNow;
            return new ClientTimeStamp(DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(), LocalSource);
        }
        finally
        {
            Gate.Release();
        }
    }

    private static async Task<TimeSpan?> TryReadOffsetAsync(Uri uri, CancellationToken cancellationToken)
    {
        DateTimeOffset before = DateTimeOffset.UtcNow;
        try
        {
            using CancellationTokenSource timeout = new(ProbeTimeout);
            using CancellationTokenSource linked = CancellationTokenSource.CreateLinkedTokenSource(
                cancellationToken,
                timeout.Token);
            using HttpRequestMessage request = new(HttpMethod.Head, uri);
            using HttpResponseMessage response = await HttpClient.SendAsync(
                request,
                HttpCompletionOption.ResponseHeadersRead,
                linked.Token).ConfigureAwait(false);
            DateTimeOffset after = DateTimeOffset.UtcNow;
            if (response.Headers.Date is not { } networkDate)
            {
                return null;
            }
            TimeSpan roundTrip = after - before;
            DateTimeOffset midpoint = before + TimeSpan.FromTicks(roundTrip.Ticks / 2);
            return networkDate - midpoint;
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

    internal sealed record ClientTimeStamp(long UnixTimeMs, string Source);
}
