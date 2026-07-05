using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Reflection;
using System.Security.Cryptography;
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

    private readonly HttpClient _httpClient = new();

    public async Task<PreparedUpdate> PrepareAsync(
        string targetDirectory,
        string evidenceDirectory,
        CancellationToken cancellationToken,
        IProgress<UpdateProgress>? progress = null)
    {
        Directory.CreateDirectory(evidenceDirectory);
        progress?.Report(UpdateProgress.Indeterminate("检查更新", "正在读取 VPS 升级清单"));
        (UpdateManifest manifest, Uri manifestUri) = await FetchManifestAsync(cancellationToken, progress).ConfigureAwait(false);
        ValidateManifest(manifest);
        string localVersion = CurrentVersion();
        progress?.Report(UpdateProgress.Indeterminate("比较版本", $"本地 {localVersion} / 服务器 {manifest.Version}"));
        if (CompareVersions(manifest.Version, localVersion) <= 0)
        {
            throw new UpdateNotNeededException(localVersion, manifest.Version);
        }

        Uri packageUri = ResolvePackageUri(manifestUri, manifest.PackageUrl, manifest.FileName);
        string workDir = Path.Combine(Path.GetTempPath(), "8ax-winremote-update", DateTime.UtcNow.ToString("yyyyMMdd_HHmmss"));
        Directory.CreateDirectory(workDir);
        string packagePath = Path.Combine(workDir, ExecutableName + ".update.zip");

        progress?.Report(UpdateProgress.PercentValue("下载更新", $"正在下载 {manifest.Version}", 0.0));
        await DownloadFileAsync(packageUri, packagePath, progress, cancellationToken).ConfigureAwait(false);
        progress?.Report(UpdateProgress.Indeterminate("校验更新", "正在校验升级包 SHA256"));
        string actualHash = Sha256File(packagePath);
        if (!String.Equals(actualHash, manifest.Sha256, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException($"Update package hash mismatch: {actualHash} != {manifest.Sha256}");
        }

        progress?.Report(UpdateProgress.Indeterminate("准备安装", "正在生成安装脚本"));
        string manifestPath = Path.Combine(workDir, "manifest.json");
        await File.WriteAllTextAsync(manifestPath, JsonSerializer.Serialize(manifest, JsonOptions()), cancellationToken).ConfigureAwait(false);
        string scriptPath = Path.Combine(workDir, "install_winremote_update.ps1");
        await File.WriteAllTextAsync(scriptPath, InstallerScript(), cancellationToken).ConfigureAwait(false);

        await File.AppendAllTextAsync(
            Path.Combine(evidenceDirectory, "winremote_update_events.jsonl"),
            JsonSerializer.Serialize(new
            {
                ts_utc = DateTimeOffset.UtcNow,
                event_name = "update_prepared",
                manifest = manifestUri.ToString(),
                package_url = packageUri.ToString(),
                version = manifest.Version,
                package_sha256 = actualHash,
                executable_name = ExecutableName,
            }) + Environment.NewLine,
            cancellationToken).ConfigureAwait(false);

        progress?.Report(UpdateProgress.PercentValue("准备完成", $"版本 {manifest.Version} 已准备好", 100.0));
        return new PreparedUpdate(manifest, manifestUri, packagePath, scriptPath);
    }

    public async Task<UpdateCheckResult> CheckAsync(
        CancellationToken cancellationToken,
        IProgress<UpdateProgress>? progress = null)
    {
        progress?.Report(UpdateProgress.Indeterminate("妫€鏌ユ洿鏂?", "姝ｅ湪璇诲彇 VPS 鍗囩骇娓呭崟"));
        (UpdateManifest manifest, Uri manifestUri) = await FetchManifestAsync(cancellationToken, progress).ConfigureAwait(false);
        ValidateManifest(manifest);
        string localVersion = CurrentVersion();
        progress?.Report(UpdateProgress.Indeterminate("姣旇緝鐗堟湰", $"鏈湴 {localVersion} / 鏈嶅姟鍣?{manifest.Version}"));
        bool isUpdateAvailable = CompareVersions(manifest.Version, localVersion) > 0;
        return new UpdateCheckResult(localVersion, manifest, manifestUri, isUpdateAvailable);
    }

    public void LaunchInstallerAndExit(PreparedUpdate update, string targetDirectory)
    {
        int pid = Environment.ProcessId;
        string manifestPath = Path.Combine(Path.GetDirectoryName(update.ScriptPath) ?? Path.GetTempPath(), "manifest.json");
        ProcessStartInfo startInfo = new()
        {
            FileName = "powershell.exe",
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        startInfo.ArgumentList.Add("-NoProfile");
        startInfo.ArgumentList.Add("-ExecutionPolicy");
        startInfo.ArgumentList.Add("Bypass");
        startInfo.ArgumentList.Add("-File");
        startInfo.ArgumentList.Add(update.ScriptPath);
        startInfo.ArgumentList.Add("-ParentPid");
        startInfo.ArgumentList.Add(pid.ToString());
        startInfo.ArgumentList.Add("-Zip");
        startInfo.ArgumentList.Add(update.PackagePath);
        startInfo.ArgumentList.Add("-Target");
        startInfo.ArgumentList.Add(targetDirectory);
        startInfo.ArgumentList.Add("-ExeName");
        startInfo.ArgumentList.Add(ExecutableName);
        startInfo.ArgumentList.Add("-Manifest");
        startInfo.ArgumentList.Add(manifestPath);
        Process.Start(startInfo);
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

    private async Task<(UpdateManifest Manifest, Uri ManifestUri)> FetchManifestAsync(
        CancellationToken cancellationToken,
        IProgress<UpdateProgress>? progress)
    {
        List<string> errors = [];
        foreach (Uri uri in DefaultManifestUris)
        {
            string sourceName = ManifestSourceName(uri);
            progress?.Report(UpdateProgress.Indeterminate("检查更新", $"正在读取 {sourceName} 升级清单"));
            try
            {
                using CancellationTokenSource timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
                timeout.CancelAfter(ManifestRequestTimeout);
                using HttpResponseMessage response = await _httpClient.GetAsync(uri, HttpCompletionOption.ResponseHeadersRead, timeout.Token).ConfigureAwait(false);
                response.EnsureSuccessStatusCode();
                string json = await response.Content.ReadAsStringAsync(timeout.Token).ConfigureAwait(false);
                json = json.TrimStart('\uFEFF');
                UpdateManifest? manifest = JsonSerializer.Deserialize<UpdateManifest>(json, JsonOptions());
                if (manifest is null)
                {
                    throw new InvalidOperationException("empty manifest");
                }

                progress?.Report(UpdateProgress.Indeterminate("检查更新", $"已读取 {sourceName} 升级清单"));
                return (manifest, uri);
            }
            catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
            {
                errors.Add($"{uri}: timeout after {ManifestRequestTimeout.TotalSeconds:0}s");
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                errors.Add($"{uri}: {ex.Message}");
            }
        }

        throw new InvalidOperationException("Unable to fetch update manifest. " + String.Join(" | ", errors));
    }

    private async Task DownloadFileAsync(Uri uri, string path, IProgress<UpdateProgress>? progress, CancellationToken cancellationToken)
    {
        using CancellationTokenSource timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(PackageDownloadTimeout);
        try
        {
            using HttpResponseMessage response = await _httpClient.GetAsync(uri, HttpCompletionOption.ResponseHeadersRead, timeout.Token).ConfigureAwait(false);
            response.EnsureSuccessStatusCode();
            long? totalBytes = response.Content.Headers.ContentLength;
            await using Stream source = await response.Content.ReadAsStreamAsync(timeout.Token).ConfigureAwait(false);
            await using FileStream target = File.Create(path);
            byte[] buffer = new byte[64 * 1024];
            long readTotal = 0;
            while (true)
            {
                int read = await source.ReadAsync(buffer.AsMemory(0, buffer.Length), timeout.Token).ConfigureAwait(false);
                if (read == 0)
                {
                    break;
                }

                await target.WriteAsync(buffer.AsMemory(0, read), timeout.Token).ConfigureAwait(false);
                readTotal += read;
                if (totalBytes is > 0)
                {
                    double percent = readTotal * 100.0 / totalBytes.Value;
                    progress?.Report(UpdateProgress.PercentValue("下载更新", $"{readTotal / 1024} KB / {totalBytes.Value / 1024} KB", percent));
                }
                else
                {
                    progress?.Report(UpdateProgress.Indeterminate("下载更新", $"{readTotal / 1024} KB"));
                }
            }
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException($"Update package download timed out after {PackageDownloadTimeout.TotalSeconds:0}s: {uri}");
        }
    }

    private static void ValidateManifest(UpdateManifest manifest)
    {
        if (!String.Equals(manifest.AppId, AppId, StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"Unexpected update app_id: {manifest.AppId}");
        }
        if (String.IsNullOrWhiteSpace(manifest.PackageUrl) && String.IsNullOrWhiteSpace(manifest.FileName))
        {
            throw new InvalidOperationException("Update manifest missing package_url/file_name");
        }
        if (String.IsNullOrWhiteSpace(manifest.Sha256))
        {
            throw new InvalidOperationException("Update manifest missing sha256");
        }
    }

    private static Uri ResolvePackageUri(Uri manifestUri, string packageUrl, string fileName)
    {
        string value = String.IsNullOrWhiteSpace(packageUrl) ? fileName : packageUrl;
        return Uri.TryCreate(value, UriKind.Absolute, out Uri? absolute)
            ? absolute
            : new Uri(manifestUri, value);
    }

    private static string Sha256File(string path)
    {
        using SHA256 sha = SHA256.Create();
        using FileStream stream = File.OpenRead(path);
        return Convert.ToHexString(sha.ComputeHash(stream));
    }

    private static string ManifestSourceName(Uri uri)
    {
        int index = Array.IndexOf(DefaultManifestUris, uri);
        return index switch
        {
            0 => $"主域名 {uri.Host}",
            1 => $"备用域名 {uri.Host}",
            _ => uri.Host,
        };
    }

    private static long[] VersionParts(string value) =>
        Regex.Matches(value, @"\d+")
            .Select(match => Int64.TryParse(match.Value, out long parsed) ? parsed : 0)
            .ToArray();

    private static JsonSerializerOptions JsonOptions() => new() { PropertyNameCaseInsensitive = true, WriteIndented = true };

    private static string InstallerScript() =>
        """
        param(
            [Parameter(Mandatory=$true)][int]$ParentPid,
            [Parameter(Mandatory=$true)][string]$Zip,
            [Parameter(Mandatory=$true)][string]$Target,
            [Parameter(Mandatory=$true)][string]$ExeName,
            [Parameter(Mandatory=$true)][string]$Manifest
        )
        $ErrorActionPreference = 'Stop'
        $logRoot = Join-Path ([IO.Path]::GetTempPath()) '8ax-winremote-update'
        New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
        $log = Join-Path $logRoot 'install.log'
        function Log([string]$Message) {
            Add-Content -LiteralPath $log -Encoding UTF8 -Value ((Get-Date -Format o) + ' ' + $Message)
        }
        Log "install begin target=$Target exe=$ExeName parent_pid=$ParentPid zip=$Zip"
        $deadline = (Get-Date).AddSeconds(60)
        while ((Get-Date) -lt $deadline) {
            $oldProcess = Get-Process -Id $ParentPid -ErrorAction SilentlyContinue
            if ($null -eq $oldProcess) { break }
            Start-Sleep -Milliseconds 500
        }
        $oldProcess = Get-Process -Id $ParentPid -ErrorAction SilentlyContinue
        if ($null -ne $oldProcess) {
            Log "old process still running; forcing stop"
            Stop-Process -Id $ParentPid -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $ParentPid -Timeout 10 -ErrorAction SilentlyContinue
        }
        Start-Sleep -Milliseconds 500
        $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
        $backupRoot = Join-Path $Target '.update_backup'
        $backup = Join-Path $backupRoot $stamp
        New-Item -ItemType Directory -Force -Path $backup | Out-Null
        Get-ChildItem -LiteralPath $Target -File | Copy-Item -Destination $backup -Force
        Log "backup=$backup"
        $stage = Join-Path ([IO.Path]::GetTempPath()) ('8ax-winremote-stage-' + $stamp)
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force -Path $stage | Out-Null
        Expand-Archive -LiteralPath $Zip -DestinationPath $stage -Force
        if (-not (Test-Path (Join-Path $stage $ExeName))) {
            throw "update package missing $ExeName"
        }
        $unexpected = @(Get-ChildItem -LiteralPath $stage -File | Where-Object { $_.Name -ne $ExeName })
        if ($unexpected.Count -ne 0) {
            throw ("single-exe update package has unexpected files: " + (($unexpected | ForEach-Object { $_.Name }) -join ', '))
        }
        foreach ($old in @('8ax.WinRemote.dll', '8ax.WinRemote.deps.json', '8ax.WinRemote.runtimeconfig.json', '8ax.WinRemote.pdb')) {
            Remove-Item -LiteralPath (Join-Path $Target $old) -Force -ErrorAction SilentlyContinue
        }
        $sourceExe = Join-Path $stage $ExeName
        $targetExe = Join-Path $Target $ExeName
        $copied = $false
        for ($attempt = 1; $attempt -le 20; $attempt++) {
            try {
                Copy-Item -LiteralPath $sourceExe -Destination $targetExe -Force -ErrorAction Stop
                $copied = $true
                Log "copy ok attempt=$attempt"
                break
            } catch {
                Log ("copy failed attempt=" + $attempt + " message=" + $_.Exception.Message)
                Start-Sleep -Milliseconds (500 * $attempt)
            }
        }
        if (-not $copied) {
            throw "update install failed: could not copy $ExeName"
        }
        for ($attempt = 1; $attempt -le 5; $attempt++) {
            try {
                $child = Start-Process -FilePath $targetExe -WorkingDirectory $Target -PassThru -ErrorAction Stop
                Start-Sleep -Milliseconds 1200
                if (-not $child.HasExited) {
                    Log "restart ok pid=$($child.Id) attempt=$attempt"
                    exit 0
                }
                Log "restart exited immediately attempt=$attempt exit=$($child.ExitCode)"
            } catch {
                Log ("restart failed attempt=" + $attempt + " message=" + $_.Exception.Message)
            }
            Start-Sleep -Milliseconds (700 * $attempt)
        }
        throw "update installed but restart failed; see $log"
        """;
}
