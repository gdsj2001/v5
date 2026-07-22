using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Reflection;
using System.Security.AccessControl;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace EightAxis.WinRemote.Update;

public sealed class WinRemoteUpdater : IDisposable
{
    public const string AppId = "8ax.WinRemote";
    public const string ExecutableName = "8ax.WinRemote.exe";
    private static readonly TimeSpan ManifestRequestTimeout = TimeSpan.FromSeconds(10);
    private static readonly TimeSpan PackageDownloadTimeout = TimeSpan.FromMinutes(2);

    public static readonly Uri[] DefaultManifestUris =
    [
        new("https://license.cjwsjzyy.xyz/8ax-winremote/win-x64/manifest.json"),
        new("https://license.3dtouch.top/8ax-winremote/win-x64/manifest.json"),
    ];

    private readonly HttpClient _httpClient;
    private readonly UpdateTrustStore _trustStore;

    public WinRemoteUpdater(HttpMessageHandler? handler = null, UpdateTrustStore? trustStore = null)
    {
        handler ??= new HttpClientHandler
        {
            AllowAutoRedirect = false,
        };
        _httpClient = new HttpClient(handler, disposeHandler: true);
        _trustStore = trustStore ?? new UpdateTrustStore();
    }

    public async Task<PreparedUpdate> PrepareAsync(
        string targetDirectory,
        string evidenceDirectory,
        CancellationToken cancellationToken,
        IProgress<UpdateProgress>? progress = null)
    {
        progress?.Report(UpdateProgress.Indeterminate("检查更新", "正在验证 VPS 签名清单"));
        VerifiedUpdateManifest verified = await FetchVerifiedManifestAsync(cancellationToken, progress).ConfigureAwait(false);
        UpdateManifest manifest = verified.Manifest;
        _trustStore.ValidateNoRollback(manifest);
        string localVersion = CurrentVersion();
        progress?.Report(UpdateProgress.Indeterminate("比较版本", $"本地 {localVersion} / 服务器 {manifest.Version}"));
        if (CompareVersions(manifest.Version, localVersion) <= 0)
        {
            throw new UpdateNotNeededException(localVersion, manifest.Version);
        }

        string workDirectory = CreateProtectedWorkDirectory(targetDirectory);
        string manifestPath = Path.Combine(workDirectory, "manifest.json");
        string signaturePath = Path.Combine(workDirectory, "manifest.sig");
        string packagePath = Path.Combine(workDirectory, manifest.FileName);
        WriteExclusive(manifestPath, verified.ManifestBytes);
        WriteExclusive(signaturePath, verified.SignatureBytes);

        Uri packageUri = ResolvePackageUri(verified.ManifestUri, manifest);
        progress?.Report(UpdateProgress.PercentValue("下载更新", $"正在下载 {manifest.Version}", 0.0));
        await DownloadPackageAsync(packageUri, packagePath, manifest, progress, cancellationToken).ConfigureAwait(false);
        progress?.Report(UpdateProgress.Indeterminate("校验更新", "正在验证签名清单绑定的大小和 SHA-256"));
        string packageHash;
        using (FileStream package = UpdateManifestVerifier.OpenExclusiveVerifiedPackage(packagePath, manifest))
        {
            packageHash = UpdateManifestVerifier.VerifyPackage(package, manifest);
        }

        string installerDirectory = Path.Combine(workDirectory, "installer");
        Directory.CreateDirectory(installerDirectory);
        string installerPath = Path.Combine(installerDirectory, ExecutableName);
        string processPath = Environment.ProcessPath
            ?? throw new InvalidOperationException("Current WinRemote executable path is unavailable.");
        if (!String.Equals(Path.GetFileName(processPath), ExecutableName, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException("Current process is not the canonical WinRemote executable.");
        }
        File.Copy(processPath, installerPath, overwrite: false);

        Directory.CreateDirectory(evidenceDirectory);
        await File.AppendAllTextAsync(
            Path.Combine(evidenceDirectory, "winremote_update_events.jsonl"),
            JsonSerializer.Serialize(new
            {
                ts_utc = DateTimeOffset.UtcNow,
                event_name = "update_prepared",
                manifest = verified.ManifestUri.ToString(),
                signature = verified.SignatureUri.ToString(),
                package_url = packageUri.ToString(),
                manifest.KeyId,
                manifest.ReleaseSequence,
                version = manifest.Version,
                package_sha256 = packageHash,
                package_size = manifest.Size,
            }) + Environment.NewLine,
            cancellationToken).ConfigureAwait(false);

        progress?.Report(UpdateProgress.PercentValue("准备完成", $"版本 {manifest.Version} 已通过签名校验", 100.0));
        return new PreparedUpdate(
            manifest, verified.ManifestUri, manifestPath, signaturePath,
            packagePath, installerPath, evidenceDirectory);
    }

    public async Task<UpdateCheckResult> CheckAsync(
        CancellationToken cancellationToken,
        IProgress<UpdateProgress>? progress = null)
    {
        progress?.Report(UpdateProgress.Indeterminate("检查更新", "正在验证 VPS 签名清单"));
        VerifiedUpdateManifest verified = await FetchVerifiedManifestAsync(cancellationToken, progress).ConfigureAwait(false);
        _trustStore.ValidateNoRollback(verified.Manifest);
        string localVersion = CurrentVersion();
        bool available = CompareVersions(verified.Manifest.Version, localVersion) > 0;
        return new UpdateCheckResult(localVersion, verified.Manifest, verified.ManifestUri, available);
    }

    public void LaunchInstallerAndExit(PreparedUpdate update, string targetDirectory)
    {
        using Process current = Process.GetCurrentProcess();
        string parentExe = Environment.ProcessPath
            ?? throw new InvalidOperationException("Current WinRemote executable path is unavailable.");
        ProcessStartInfo startInfo = new()
        {
            FileName = update.InstallerPath,
            WorkingDirectory = Path.GetDirectoryName(update.InstallerPath),
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        AddArgument(startInfo, "--apply-update");
        AddArgument(startInfo, "--parent-pid", current.Id.ToString());
        AddArgument(startInfo, "--parent-start-utc-ticks", current.StartTime.ToUniversalTime().Ticks.ToString());
        AddArgument(startInfo, "--parent-exe", parentExe);
        AddArgument(startInfo, "--target-dir", Path.GetFullPath(targetDirectory));
        AddArgument(startInfo, "--manifest", update.ManifestPath);
        AddArgument(startInfo, "--signature", update.SignaturePath);
        AddArgument(startInfo, "--package", update.PackagePath);
        AddArgument(startInfo, "--evidence-dir", update.EvidenceDirectory);
        _ = Process.Start(startInfo) ?? throw new InvalidOperationException("Update installer process did not start.");
    }

    public void Dispose() => _httpClient.Dispose();

    public static string CurrentVersion()
    {
        Assembly assembly = typeof(WinRemoteUpdater).Assembly;
        string? informational = assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion;
        if (!String.IsNullOrWhiteSpace(informational))
        {
            int metadataIndex = informational.IndexOf('+', StringComparison.Ordinal);
            return metadataIndex > 0 ? informational[..metadataIndex] : informational;
        }
        return assembly.GetName().Version?.ToString() ?? "0.0.0";
    }

    public static int CompareVersions(string left, string right)
    {
        long[] leftParts = VersionParts(left);
        long[] rightParts = VersionParts(right);
        int count = Math.Max(leftParts.Length, rightParts.Length);
        for (int index = 0; index < count; index++)
        {
            long leftValue = index < leftParts.Length ? leftParts[index] : 0;
            long rightValue = index < rightParts.Length ? rightParts[index] : 0;
            int comparison = leftValue.CompareTo(rightValue);
            if (comparison != 0)
            {
                return comparison;
            }
        }
        return StringComparer.OrdinalIgnoreCase.Compare(left, right);
    }

    private async Task<VerifiedUpdateManifest> FetchVerifiedManifestAsync(
        CancellationToken cancellationToken,
        IProgress<UpdateProgress>? progress)
    {
        List<VerifiedUpdateManifest> verified = [];
        List<string> errors = [];
        foreach (Uri manifestUri in DefaultManifestUris)
        {
            Uri signatureUri = new(manifestUri, "manifest.sig");
            progress?.Report(UpdateProgress.Indeterminate("检查更新", $"正在读取 {manifestUri.Host} 签名清单"));
            try
            {
                byte[] manifestBytes = await GetBoundedBytesAsync(
                    manifestUri, UpdateManifestVerifier.MaxManifestBytes, cancellationToken).ConfigureAwait(false);
                byte[] signatureBytes = await GetBoundedBytesAsync(
                    signatureUri, UpdateManifestVerifier.MaxSignatureBytes, cancellationToken).ConfigureAwait(false);
                UpdateManifest manifest = UpdateManifestVerifier.VerifyAndParse(manifestBytes, signatureBytes);
                verified.Add(new VerifiedUpdateManifest(
                    manifest, manifestUri, signatureUri, manifestBytes, signatureBytes));
            }
            catch (Exception ex) when (ex is not OperationCanceledException || !cancellationToken.IsCancellationRequested)
            {
                errors.Add($"{manifestUri.Host}: {ex.Message}");
            }
        }
        if (verified.Count == 0)
        {
            throw new InvalidOperationException("Unable to fetch a verified update manifest. " + String.Join(" | ", errors));
        }
        VerifiedUpdateManifest selected = verified[0];
        foreach (VerifiedUpdateManifest candidate in verified.Skip(1))
        {
            if (!CryptographicOperations.FixedTimeEquals(selected.ManifestBytes, candidate.ManifestBytes) ||
                !CryptographicOperations.FixedTimeEquals(selected.SignatureBytes, candidate.SignatureBytes))
            {
                throw new InvalidOperationException("Primary and backup update manifests/signatures disagree.");
            }
        }
        return selected;
    }

    private async Task<byte[]> GetBoundedBytesAsync(Uri uri, int maximumBytes, CancellationToken cancellationToken)
    {
        RequireHttps(uri);
        using CancellationTokenSource timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(ManifestRequestTimeout);
        using HttpResponseMessage response = await _httpClient.GetAsync(
            uri, HttpCompletionOption.ResponseHeadersRead, timeout.Token).ConfigureAwait(false);
        if (IsRedirect(response.StatusCode))
        {
            throw new InvalidOperationException($"Update endpoint redirects are forbidden: {uri}.");
        }
        response.EnsureSuccessStatusCode();
        if (response.Content.Headers.ContentLength is long length && (length <= 0 || length > maximumBytes))
        {
            throw new InvalidOperationException($"Update endpoint response length is invalid: {uri}.");
        }
        await using Stream source = await response.Content.ReadAsStreamAsync(timeout.Token).ConfigureAwait(false);
        using MemoryStream target = new();
        byte[] buffer = new byte[8192];
        while (true)
        {
            int read = await source.ReadAsync(buffer, timeout.Token).ConfigureAwait(false);
            if (read == 0)
            {
                break;
            }
            if (target.Length + read > maximumBytes)
            {
                throw new InvalidOperationException($"Update endpoint response exceeds its limit: {uri}.");
            }
            target.Write(buffer, 0, read);
        }
        if (target.Length == 0)
        {
            throw new InvalidOperationException($"Update endpoint response is empty: {uri}.");
        }
        return target.ToArray();
    }

    private async Task DownloadPackageAsync(
        Uri uri,
        string path,
        UpdateManifest manifest,
        IProgress<UpdateProgress>? progress,
        CancellationToken cancellationToken)
    {
        RequireHttps(uri);
        using CancellationTokenSource timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(PackageDownloadTimeout);
        using HttpResponseMessage response = await _httpClient.GetAsync(
            uri, HttpCompletionOption.ResponseHeadersRead, timeout.Token).ConfigureAwait(false);
        if (IsRedirect(response.StatusCode))
        {
            throw new InvalidOperationException("Update package redirects are forbidden.");
        }
        response.EnsureSuccessStatusCode();
        if (response.Content.Headers.ContentLength is long advertised && advertised != manifest.Size)
        {
            throw new InvalidOperationException($"Update package Content-Length mismatch: {advertised} != {manifest.Size}.");
        }
        await using Stream source = await response.Content.ReadAsStreamAsync(timeout.Token).ConfigureAwait(false);
        await using FileStream target = new(
            path, FileMode.CreateNew, FileAccess.Write, FileShare.None,
            64 * 1024, FileOptions.WriteThrough);
        byte[] buffer = new byte[64 * 1024];
        long total = 0;
        while (true)
        {
            int read = await source.ReadAsync(buffer, timeout.Token).ConfigureAwait(false);
            if (read == 0)
            {
                break;
            }
            total += read;
            if (total > manifest.Size)
            {
                throw new InvalidOperationException("Update package exceeds signed size.");
            }
            await target.WriteAsync(buffer.AsMemory(0, read), timeout.Token).ConfigureAwait(false);
            progress?.Report(UpdateProgress.PercentValue(
                "下载更新", $"{total / 1024} KB / {manifest.Size / 1024} KB", total * 100.0 / manifest.Size));
        }
        await target.FlushAsync(timeout.Token).ConfigureAwait(false);
        target.Flush(flushToDisk: true);
        if (total != manifest.Size)
        {
            throw new InvalidOperationException($"Update package downloaded size mismatch: {total} != {manifest.Size}.");
        }
    }

    private static Uri ResolvePackageUri(Uri manifestUri, UpdateManifest manifest)
    {
        Uri resolved = Uri.TryCreate(manifest.PackageUrl, UriKind.Absolute, out Uri? absolute)
            ? absolute
            : new Uri(manifestUri, manifest.PackageUrl);
        RequireHttps(resolved);
        if (!String.IsNullOrEmpty(resolved.UserInfo) || !String.IsNullOrEmpty(resolved.Fragment) ||
            !String.Equals(Path.GetFileName(resolved.AbsolutePath), manifest.FileName, StringComparison.Ordinal))
        {
            throw new InvalidOperationException("Update package URL identity is invalid.");
        }
        return resolved;
    }

    private static string CreateProtectedWorkDirectory(string targetDirectory)
    {
        string targetRoot = Path.GetFullPath(targetDirectory);
        if (!Directory.Exists(targetRoot) ||
            (File.GetAttributes(targetRoot) & FileAttributes.ReparsePoint) != 0)
        {
            throw new InvalidOperationException("Update target directory identity is invalid.");
        }
        // File.Replace requires the staged executable and destination to be on
        // the same volume. Keep the protected random work directory beside the
        // installed executable so updates also work when WinRemote is not on C:.
        string root = Path.Combine(targetRoot, ".winremote-update");
        Directory.CreateDirectory(root);
        if ((File.GetAttributes(root) & FileAttributes.ReparsePoint) != 0)
        {
            throw new InvalidOperationException("Update staging root must not be a reparse point.");
        }
        string path = Path.Combine(root, Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(path);
        SecurityIdentifier current = WindowsIdentity.GetCurrent().User
            ?? throw new InvalidOperationException("Current Windows user SID is unavailable.");
        SecurityIdentifier system = new(WellKnownSidType.LocalSystemSid, null);
        DirectorySecurity security = new();
        security.SetAccessRuleProtection(isProtected: true, preserveInheritance: false);
        InheritanceFlags inheritance = InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit;
        security.AddAccessRule(new FileSystemAccessRule(current, FileSystemRights.FullControl, inheritance,
            PropagationFlags.None, AccessControlType.Allow));
        security.AddAccessRule(new FileSystemAccessRule(system, FileSystemRights.FullControl, inheritance,
            PropagationFlags.None, AccessControlType.Allow));
        new DirectoryInfo(path).SetAccessControl(security);
        return path;
    }

    private static void WriteExclusive(string path, byte[] bytes)
    {
        using FileStream stream = new(path, FileMode.CreateNew, FileAccess.Write, FileShare.None,
            4096, FileOptions.WriteThrough);
        stream.Write(bytes);
        stream.Flush(flushToDisk: true);
    }

    private static void AddArgument(ProcessStartInfo startInfo, params string[] values)
    {
        foreach (string value in values)
        {
            startInfo.ArgumentList.Add(value);
        }
    }

    private static void RequireHttps(Uri uri)
    {
        if (!uri.IsAbsoluteUri || uri.Scheme != Uri.UriSchemeHttps || !String.IsNullOrEmpty(uri.UserInfo))
        {
            throw new InvalidOperationException("Update endpoints must use HTTPS without userinfo.");
        }
    }

    private static bool IsRedirect(HttpStatusCode statusCode) => (int)statusCode is >= 300 and < 400;

    private static long[] VersionParts(string value) =>
        Regex.Matches(value, @"\d+")
            .Select(match => Int64.TryParse(match.Value, out long parsed) ? parsed : 0)
            .ToArray();
}
