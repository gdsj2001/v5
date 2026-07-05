using System.Text.Json.Serialization;

namespace EightAxis.WinRemote.Update;

public sealed record UpdateManifest
{
    [JsonPropertyName("app_id")]
    public string AppId { get; init; } = "";

    [JsonPropertyName("version")]
    public string Version { get; init; } = "";

    [JsonPropertyName("file_name")]
    public string FileName { get; init; } = "";

    [JsonPropertyName("package_url")]
    public string PackageUrl { get; init; } = "";

    [JsonPropertyName("sha256")]
    public string Sha256 { get; init; } = "";

    [JsonPropertyName("size")]
    public long Size { get; init; }

    [JsonPropertyName("published_at_utc")]
    public string PublishedAtUtc { get; init; } = "";
}

public sealed record PreparedUpdate(UpdateManifest Manifest, Uri ManifestUri, string PackagePath, string ScriptPath);

public sealed record UpdateCheckResult(string LocalVersion, UpdateManifest Manifest, Uri ManifestUri, bool IsUpdateAvailable);

public sealed class UpdateNotNeededException : Exception
{
    public UpdateNotNeededException(string localVersion, string remoteVersion)
        : base($"Already up to date: local {localVersion}, server {remoteVersion}")
    {
        LocalVersion = localVersion;
        RemoteVersion = remoteVersion;
    }

    public string LocalVersion { get; }

    public string RemoteVersion { get; }
}

public sealed record UpdateProgress(string Stage, string Message, double? Percent = null)
{
    public static UpdateProgress Indeterminate(string stage, string message) => new(stage, message, null);

    public static UpdateProgress PercentValue(string stage, string message, double percent) =>
        new(stage, message, Math.Clamp(percent, 0.0, 100.0));
}
