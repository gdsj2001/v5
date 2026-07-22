using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Text.Json;

namespace EightAxis.WinRemote.Update;

public static class UpdateInstaller
{
    private const string ApplySwitch = "--apply-update";
    private static readonly TimeSpan ParentExitTimeout = TimeSpan.FromSeconds(60);
    private static readonly TimeSpan RestartProbeTime = TimeSpan.FromMilliseconds(1200);

    public static bool IsApplyUpdateInvocation(IReadOnlyList<string> args) =>
        args.Count > 0 && String.Equals(args[0], ApplySwitch, StringComparison.Ordinal);

    public static int Run(IReadOnlyList<string> args)
    {
        string evidenceDirectory = ReadOptionalArgument(args, "--evidence-dir") ?? Path.GetTempPath();
        try
        {
            ApplyArguments parsed = ParseArguments(args);
            evidenceDirectory = parsed.EvidenceDirectory;
            Apply(parsed);
            AppendEvidence(evidenceDirectory, "update_install_succeeded", null);
            return 0;
        }
        catch (Exception ex)
        {
            try
            {
                AppendEvidence(evidenceDirectory, "update_install_failed", ex.Message);
            }
            catch
            {
            }
            return 20;
        }
    }

    private static void Apply(ApplyArguments args)
    {
        string targetDirectory = Path.GetFullPath(args.TargetDirectory);
        if (!Directory.Exists(targetDirectory) ||
            (File.GetAttributes(targetDirectory) & FileAttributes.ReparsePoint) != 0)
        {
            throw new InvalidOperationException("Update target directory identity is invalid.");
        }
        string targetExe = Path.Combine(targetDirectory, WinRemoteUpdater.ExecutableName);
        if (!File.Exists(targetExe) ||
            (File.GetAttributes(targetExe) & FileAttributes.ReparsePoint) != 0)
        {
            throw new InvalidOperationException("Installed WinRemote executable identity is invalid.");
        }
        if (!String.Equals(Path.GetFullPath(args.ParentExecutable), targetExe, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException("Update parent executable does not match the install target.");
        }

        WaitForParent(args.ParentPid, args.ParentStartUtcTicks, targetExe);

        byte[] manifestBytes = ReadBoundedFile(args.ManifestPath, UpdateManifestVerifier.MaxManifestBytes);
        byte[] signatureBytes = ReadBoundedFile(args.SignaturePath, UpdateManifestVerifier.MaxSignatureBytes);
        UpdateManifest manifest = UpdateManifestVerifier.VerifyAndParse(manifestBytes, signatureBytes);
        UpdateTrustStore trustStore = new();
        trustStore.ValidateNoRollback(manifest);

        string workDirectory = Path.GetDirectoryName(Path.GetFullPath(args.PackagePath))
            ?? throw new InvalidOperationException("Update work directory is invalid.");
        string stageDirectory = Path.Combine(workDirectory, "stage-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(stageDirectory);
        string stagedExe = Path.Combine(stageDirectory, WinRemoteUpdater.ExecutableName);

        using (FileStream package = UpdateManifestVerifier.OpenExclusiveVerifiedPackage(args.PackagePath, manifest))
        using (ZipArchive archive = new(package, ZipArchiveMode.Read, leaveOpen: true))
        {
            if (archive.Entries.Count != 1)
            {
                throw new InvalidOperationException("Update package must contain exactly one executable.");
            }
            ZipArchiveEntry entry = archive.Entries[0];
            if (entry.FullName != WinRemoteUpdater.ExecutableName ||
                entry.Name != WinRemoteUpdater.ExecutableName || entry.Length <= 0)
            {
                throw new InvalidOperationException("Update package entry identity is invalid.");
            }
            using Stream source = entry.Open();
            using FileStream destination = new(
                stagedExe, FileMode.CreateNew, FileAccess.Write, FileShare.None,
                64 * 1024, FileOptions.WriteThrough);
            source.CopyTo(destination);
            destination.Flush(flushToDisk: true);
        }

        VerifyExecutableIdentity(stagedExe, manifest);
        string stamp = DateTime.UtcNow.ToString("yyyyMMdd_HHmmss");
        string backupDirectory = Path.Combine(targetDirectory, ".update_backup", stamp);
        Directory.CreateDirectory(backupDirectory);
        string backupExe = Path.Combine(backupDirectory, WinRemoteUpdater.ExecutableName);
        string rejectedExe = Path.Combine(backupDirectory, "rejected-new-" + WinRemoteUpdater.ExecutableName);

        File.Replace(stagedExe, targetExe, backupExe, ignoreMetadataErrors: true);
        Process? child = null;
        try
        {
            child = Process.Start(new ProcessStartInfo
            {
                FileName = targetExe,
                WorkingDirectory = targetDirectory,
                UseShellExecute = false,
            }) ?? throw new InvalidOperationException("Updated WinRemote process did not start.");
            if (child.WaitForExit((int)RestartProbeTime.TotalMilliseconds))
            {
                throw new InvalidOperationException($"Updated WinRemote exited immediately with code {child.ExitCode}.");
            }
            trustStore.RecordInstalled(manifest);
        }
        catch
        {
            if (child is { HasExited: false })
            {
                child.Kill(entireProcessTree: true);
                child.WaitForExit(5000);
            }
            RollBack(targetExe, backupExe, rejectedExe, targetDirectory);
            throw;
        }
        finally
        {
            child?.Dispose();
        }
    }

    private static void WaitForParent(int parentPid, long expectedStartUtcTicks, string expectedPath)
    {
        if (parentPid <= 0 || expectedStartUtcTicks <= 0)
        {
            throw new InvalidOperationException("Update parent process identity is invalid.");
        }
        Process? parent;
        try
        {
            parent = Process.GetProcessById(parentPid);
        }
        catch (ArgumentException)
        {
            return;
        }
        using (parent)
        {
            if (!String.Equals(parent.MainModule?.FileName, expectedPath, StringComparison.OrdinalIgnoreCase) ||
                parent.StartTime.ToUniversalTime().Ticks != expectedStartUtcTicks)
            {
                throw new InvalidOperationException("Update parent process identity changed.");
            }
            if (!parent.WaitForExit((int)ParentExitTimeout.TotalMilliseconds))
            {
                throw new TimeoutException("WinRemote parent process did not exit before update installation.");
            }
        }
    }

    private static void VerifyExecutableIdentity(string executablePath, UpdateManifest manifest)
    {
        FileVersionInfo version = FileVersionInfo.GetVersionInfo(executablePath);
        string productName = version.ProductName ?? "";
        string productVersion = (version.ProductVersion ?? "").Split('+', 2)[0];
        if (productName != WinRemoteUpdater.AppId || productVersion != manifest.Version)
        {
            throw new InvalidOperationException(
                $"Update executable identity mismatch: product={productName}, version={productVersion}.");
        }
    }

    private static void RollBack(string targetExe, string backupExe, string rejectedExe, string targetDirectory)
    {
        if (!File.Exists(backupExe))
        {
            throw new InvalidOperationException("Update rollback backup is missing.");
        }
        File.Replace(backupExe, targetExe, rejectedExe, ignoreMetadataErrors: true);
        _ = Process.Start(new ProcessStartInfo
        {
            FileName = targetExe,
            WorkingDirectory = targetDirectory,
            UseShellExecute = false,
        });
    }

    private static byte[] ReadBoundedFile(string path, int maximumBytes)
    {
        FileInfo info = new(Path.GetFullPath(path));
        if (!info.Exists || (info.Attributes & FileAttributes.ReparsePoint) != 0 ||
            info.Length <= 0 || info.Length > maximumBytes)
        {
            throw new InvalidOperationException("Update verification input file identity is invalid.");
        }
        using FileStream stream = new(info.FullName, FileMode.Open, FileAccess.Read, FileShare.Read);
        byte[] bytes = new byte[checked((int)info.Length)];
        stream.ReadExactly(bytes);
        return bytes;
    }

    private static ApplyArguments ParseArguments(IReadOnlyList<string> args)
    {
        if (!IsApplyUpdateInvocation(args) || args.Count != 17)
        {
            throw new InvalidOperationException("Update installer arguments are invalid.");
        }
        Dictionary<string, string> values = new(StringComparer.Ordinal);
        for (int index = 1; index < args.Count; index += 2)
        {
            if (!args[index].StartsWith("--", StringComparison.Ordinal) ||
                !values.TryAdd(args[index], args[index + 1]))
            {
                throw new InvalidOperationException("Update installer arguments are duplicated or malformed.");
            }
        }
        string[] expected =
        [
            "--parent-pid", "--parent-start-utc-ticks", "--parent-exe", "--target-dir",
            "--manifest", "--signature", "--package", "--evidence-dir",
        ];
        if (values.Count != expected.Length || expected.Any(name => !values.ContainsKey(name)) ||
            !Int32.TryParse(values["--parent-pid"], out int parentPid) ||
            !Int64.TryParse(values["--parent-start-utc-ticks"], out long parentStart))
        {
            throw new InvalidOperationException("Update installer argument values are invalid.");
        }
        return new ApplyArguments(
            parentPid, parentStart, values["--parent-exe"], values["--target-dir"],
            values["--manifest"], values["--signature"], values["--package"], values["--evidence-dir"]);
    }

    private static string? ReadOptionalArgument(IReadOnlyList<string> args, string name)
    {
        for (int index = 1; index + 1 < args.Count; index += 2)
        {
            if (args[index] == name)
            {
                return args[index + 1];
            }
        }
        return null;
    }

    private static void AppendEvidence(string directory, string eventName, string? error)
    {
        Directory.CreateDirectory(directory);
        File.AppendAllText(
            Path.Combine(directory, "winremote_update_events.jsonl"),
            JsonSerializer.Serialize(new { ts_utc = DateTimeOffset.UtcNow, event_name = eventName, error }) + Environment.NewLine);
    }

    private sealed record ApplyArguments(
        int ParentPid,
        long ParentStartUtcTicks,
        string ParentExecutable,
        string TargetDirectory,
        string ManifestPath,
        string SignaturePath,
        string PackagePath,
        string EvidenceDirectory);
}
