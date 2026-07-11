using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;
using EightAxis.WinRemote.Config;
using EightAxis.WinRemote.Diagnostics;
using EightAxis.WinRemote.Input;
using EightAxis.WinRemote.Protocol;
using EightAxis.WinRemote.Rendering;
using EightAxis.WinRemote.Transport;
using EightAxis.WinRemote.Update;
using Microsoft.Win32;
using System.IO;
using System.Security.Cryptography;

namespace EightAxis.WinRemote;

public partial class MainWindow
{
    private async void UpgradeButton_OnClick(object sender, RoutedEventArgs e)
    {
        UpgradeButton.IsEnabled = false;
        UpgradeButton.Content = "\u68c0\u67e5\u4e2d";
        UpdateProgressDialog progressDialog = new() { Owner = this };
        progressDialog.Report(UpdateProgress.Indeterminate("检查更新", "正在连接 VPS"));
        progressDialog.Show();
        progressDialog.Activate();
        await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
        Progress<UpdateProgress> progress = new(item =>
        {
            if (progressDialog.IsVisible)
            {
                progressDialog.Report(item);
                progressDialog.Activate();
            }
        });
        try
        {
            SetConnectionState("updating", "#3B3525", "#F1D79A");
            _evidence.RecordEvent("update_check", new Dictionary<string, object?>
            {
                ["source"] = "button",
                ["exe_name"] = WinRemoteUpdater.ExecutableName,
            });
            PreparedUpdate update = await _updater.PrepareAsync(AppContext.BaseDirectory, _settings.EvidenceDirectory, _shutdown.Token, progress);
            _evidence.RecordEvent("update_ready", new Dictionary<string, object?>
            {
                ["version"] = update.Manifest.Version,
                ["manifest"] = update.ManifestUri,
                ["package"] = update.PackagePath,
                ["exe_name"] = WinRemoteUpdater.ExecutableName,
            });
            UpgradeButton.Content = "\u91cd\u542f\u4e2d";
            progressDialog.Report(UpdateProgress.PercentValue("重启安装", $"正在安装 {update.Manifest.Version}", 100.0));
            StatusText.Text = $"update ready: {update.Manifest.Version}  restarting {WinRemoteUpdater.ExecutableName}";
            _updater.LaunchInstallerAndExit(update, AppContext.BaseDirectory);
            _shutdown.Cancel();
            await Task.Delay(300);
            Application.Current.Shutdown(0);
        }
        catch (UpdateNotNeededException ex)
        {
            _evidence.RecordEvent("update_not_needed", new Dictionary<string, object?>
            {
                ["local_version"] = ex.LocalVersion,
                ["remote_version"] = ex.RemoteVersion,
            });
            _updateAvailable = false;
            SetConnectionState("live", "#263D30", "#A8E8B2");
            StatusText.Text = $"already up to date: local {ex.LocalVersion}, server {ex.RemoteVersion}  {RemotePointerStatusText}";
            progressDialog.MarkDone($"本地版本 {ex.LocalVersion} 已是最新。服务器版本 {ex.RemoteVersion}。");
            RefreshUpgradeButtonText();
        }
        catch (Exception ex)
        {
            _evidence.RecordEvent("update_failed", new Dictionary<string, object?>
            {
                ["message"] = ex.Message,
            });
            SetConnectionState("error", "#3B2730", "#FFB3B3");
            StatusText.Text = $"update failed: {Compact(ex.Message)}  {RemotePointerStatusText}";
            progressDialog.MarkFailed(Compact(ex.Message));
            RefreshUpgradeButtonText();
        }
    }

    private async Task CheckForVpsUpdateAsync()
    {
        try
        {
            UpdateCheckResult check = await _updater.CheckAsync(_shutdown.Token);
            await Dispatcher.InvokeAsync(() =>
            {
                _updateAvailable = check.IsUpdateAvailable;
                RefreshUpgradeButtonText();

                UpgradeButton.ToolTip = check.IsUpdateAvailable
                    ? $"server {check.Manifest.Version} / local {check.LocalVersion}"
                    : $"up to date: {check.LocalVersion}";
                _evidence.RecordEvent(check.IsUpdateAvailable ? "update_available" : "update_current", new Dictionary<string, object?>
                {
                    ["local_version"] = check.LocalVersion,
                    ["remote_version"] = check.Manifest.Version,
                    ["manifest"] = check.ManifestUri,
                });
            });
        }
        catch (OperationCanceledException) when (_shutdown.IsCancellationRequested)
        {
        }
        catch (Exception ex)
        {
            await Dispatcher.InvokeAsync(() =>
            {
                _updateAvailable = false;
                RefreshUpgradeButtonText();
                UpgradeButton.ToolTip = $"update check failed: {Compact(ex.Message)}";
                _evidence.RecordEvent("update_check_hint_failed", new Dictionary<string, object?>
                {
                    ["message"] = ex.Message,
                });
            });
        }
    }

    private void RefreshUpgradeButtonText()
    {
        UpgradeButton.Content = "\u5347\u7ea7";
        UpgradeButton.Visibility = _updateAvailable ? Visibility.Visible : Visibility.Collapsed;
        UpgradeButton.IsEnabled = _updateAvailable;
    }

    private async void ReadLogButton_OnClick(object sender, RoutedEventArgs e)
    {
        ReadLogButton.IsEnabled = false;
        try
        {
            _evidence.RecordEvent("board_diagnostics_requested", new Dictionary<string, object?>
            {
                ["relay"] = _settings.RelayBaseUri,
            });
            string diagnostics = await FetchBoardDiagnosticsAsync();
            _evidence.RecordEvent("board_diagnostics_received", new Dictionary<string, object?>
            {
                ["relay"] = _settings.RelayBaseUri,
                ["bytes"] = diagnostics.Length,
            });
            ClientLogPreviewDialog dialog = new(
                "\u677f\u7aef\u8fd0\u884c\u8bca\u65ad",
                $"relay: {_settings.RelayBaseUri}\nendpoint: /remote/diagnostics\ncaptured: {DateTimeOffset.Now:yyyy-MM-dd HH:mm:ss zzz}",
                diagnostics,
                FetchBoardDiagnosticsAsync)
            {
                Owner = this,
            };
            dialog.Show();
            dialog.Activate();
        }
        catch (Exception ex) when (ex is not OperationCanceledException || !_shutdown.IsCancellationRequested)
        {
            _evidence.RecordEvent("board_diagnostics_failed", new Dictionary<string, object?>
            {
                ["relay"] = _settings.RelayBaseUri,
                ["message"] = ex.Message,
            });
            StatusText.Text = $"board diagnostics failed: {Compact(ex.Message)}  {RemotePointerStatusText}";
            MessageBox.Show(this, Compact(ex.Message), "\u8bfb\u53d6\u65e5\u5fd7", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
        finally
        {
            ReadLogButton.IsEnabled = true;
        }
    }

    private async void UploadGCodeButton_OnClick(object sender, RoutedEventArgs e)
    {
        OpenFileDialog dialog = new()
        {
            Title = "\u9009\u62e9G\u4ee3\u7801\u6587\u4ef6",
            Filter = "G-code files (*.ngc;*.nc;*.tap;*.gcode)|*.ngc;*.nc;*.tap;*.gcode|All files (*.*)|*.*",
            CheckFileExists = true,
            Multiselect = false,
        };
        if (dialog.ShowDialog(this) != true)
        {
            _evidence.RecordEvent("gcode_upload_cancelled", new Dictionary<string, object?>
            {
                ["reason"] = "file_picker_cancelled",
            });
            return;
        }

        UploadGCodeButton.IsEnabled = false;
        string localPath = dialog.FileName;
        string fileName = Path.GetFileName(localPath);
        try
        {
            FileInfo fileInfo = new(localPath);
            bool overwrite = await ConfirmProgramOverwriteIfNeededAsync(fileName);
            await using FileStream stream = new(localPath, FileMode.Open, FileAccess.Read, FileShare.Read);
            string sha256 = await ComputeSha256HexAsync(stream, _shutdown.Token);
            stream.Position = 0;
            _evidence.RecordEvent("gcode_upload_started", new Dictionary<string, object?>
            {
                ["relay"] = _settings.RelayBaseUri,
                ["file_name"] = fileName,
                ["size_bytes"] = fileInfo.Length,
                ["sha256"] = sha256,
                ["overwrite"] = overwrite,
            });

            ProgramUploadResult result = await UploadProgramAsync(fileName, stream, fileInfo.Length, sha256, overwrite);
            if (!String.Equals(result.Sha256, sha256, StringComparison.OrdinalIgnoreCase) || result.SizeBytes != fileInfo.Length)
            {
                throw new InvalidOperationException("Board upload readback did not match the selected file.");
            }

            _evidence.RecordEvent("gcode_upload_uploaded", new Dictionary<string, object?>
            {
                ["relay"] = _settings.RelayBaseUri,
                ["file_name"] = result.FileName,
                ["destination_path"] = result.DestinationPath,
                ["size_bytes"] = result.SizeBytes,
                ["sha256"] = result.Sha256,
                ["overwrote"] = result.Overwrote,
            });
            StatusText.Text = $"uploaded G-code: {result.FileName} {result.SizeBytes} bytes sha256 {ShortSha(result.Sha256)}  not opened or run  {RemotePointerStatusText}";
            MessageBox.Show(
                this,
                $"\u5df2\u4e0a\u4f20\u5230\u677f\u7aef\u7a0b\u5e8f\u76ee\u5f55\uff1a\n{result.DestinationPath}\n\nSHA256: {result.Sha256}\n\n\u7a0b\u5e8f\u5217\u8868\u4f1a\u81ea\u52a8\u5237\u65b0\u663e\u793a\uff0c\u672a\u6253\u5f00\uff0c\u672a\u8fd0\u884c\u3002",
                "\u4f20G\u4ee3\u7801",
                MessageBoxButton.OK,
                MessageBoxImage.Information);
        }
        catch (OperationCanceledException ex) when (!_shutdown.IsCancellationRequested)
        {
            _evidence.RecordEvent("gcode_upload_cancelled", new Dictionary<string, object?>
            {
                ["relay"] = _settings.RelayBaseUri,
                ["file_name"] = fileName,
                ["reason"] = ex.Message,
            });
            StatusText.Text = $"G-code upload cancelled: {Compact(ex.Message)}  {RemotePointerStatusText}";
        }
        catch (Exception ex) when (ex is not OperationCanceledException || !_shutdown.IsCancellationRequested)
        {
            _evidence.RecordEvent("gcode_upload_failed", new Dictionary<string, object?>
            {
                ["relay"] = _settings.RelayBaseUri,
                ["file_name"] = fileName,
                ["message"] = ex.Message,
            });
            StatusText.Text = $"G-code upload failed: {Compact(ex.Message)}  {RemotePointerStatusText}";
            MessageBox.Show(this, Compact(ex.Message), "\u4f20G\u4ee3\u7801", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
        finally
        {
            UploadGCodeButton.IsEnabled = true;
        }
    }

    private void OpenSystemGCodeButton_OnClick(object sender, RoutedEventArgs e)
    {
        OpenSystemGCodeButton.IsEnabled = false;
        try
        {
            _evidence.RecordEvent("system_gcode_directory_opened", new Dictionary<string, object?>
            {
                ["relay"] = _settings.RelayBaseUri,
            });

            ProgramDirectoryDialog dialog = new(
                async () =>
                {
                    ProgramListResult result = await GetProgramListAsync();
                    _evidence.RecordEvent("system_gcode_directory_listed", new Dictionary<string, object?>
                    {
                        ["relay"] = _settings.RelayBaseUri,
                        ["count"] = result.Count,
                        ["program_dir"] = result.ProgramDir,
                    });
                    return result;
                },
                GetProgramFileInfoAsync,
                async fileName =>
                {
                    ProgramFileContentResult result = await GetProgramFileContentAsync(fileName);
                    _evidence.RecordEvent("system_gcode_file_opened", new Dictionary<string, object?>
                    {
                        ["relay"] = _settings.RelayBaseUri,
                        ["file_name"] = result.FileName,
                        ["size_bytes"] = result.SizeBytes,
                        ["sha256"] = result.Sha256,
                    });
                    return result;
                },
                async fileName =>
                {
                    ProgramDeleteResult result = await DeleteProgramFileAsync(fileName);
                    _evidence.RecordEvent("system_gcode_file_deleted", new Dictionary<string, object?>
                    {
                        ["relay"] = _settings.RelayBaseUri,
                        ["file_name"] = result.FileName,
                        ["deleted"] = result.Deleted,
                    });
                    return result;
                },
                async (fileName, stream, length, sha256, overwrite) =>
                {
                    ProgramUploadResult result = await UploadProgramAsync(fileName, stream, length, sha256, overwrite);
                    _evidence.RecordEvent("system_gcode_file_uploaded", new Dictionary<string, object?>
                    {
                        ["relay"] = _settings.RelayBaseUri,
                        ["file_name"] = result.FileName,
                        ["destination_path"] = result.DestinationPath,
                        ["size_bytes"] = result.SizeBytes,
                        ["sha256"] = result.Sha256,
                        ["overwrote"] = result.Overwrote,
                    });
                    return result;
                })
            {
                Owner = this,
            };
            dialog.Closed += (_, _) => OpenSystemGCodeButton.IsEnabled = true;
            dialog.Show();
            dialog.Activate();
        }
        catch (Exception ex)
        {
            OpenSystemGCodeButton.IsEnabled = true;
            _evidence.RecordEvent("system_gcode_directory_failed", new Dictionary<string, object?>
            {
                ["relay"] = _settings.RelayBaseUri,
                ["message"] = ex.Message,
            });
            StatusText.Text = $"system G-code directory failed: {Compact(ex.Message)}  {RemotePointerStatusText}";
            MessageBox.Show(this, Compact(ex.Message), "\u6253\u5f00\u7cfb\u7edfG\u4ee3\u7801", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    private async Task<bool> ConfirmProgramOverwriteIfNeededAsync(string fileName)
    {
        ProgramFileInfo existing = await GetProgramFileInfoAsync(fileName);
        if (!existing.Exists)
        {
            return false;
        }

        MessageBoxResult answer = MessageBox.Show(
            this,
            $"\u677f\u7aef\u7a0b\u5e8f\u76ee\u5f55\u5df2\u7ecf\u6709\u540c\u540d\u6587\u4ef6\uff1a{fileName}\n\u662f\u5426\u8986\u76d6\uff1f",
            "\u4f20G\u4ee3\u7801",
            MessageBoxButton.YesNo,
            MessageBoxImage.Warning);
        if (answer != MessageBoxResult.Yes)
        {
            throw new OperationCanceledException("\u7528\u6237\u53d6\u6d88\u8986\u76d6\u540c\u540dG\u4ee3\u7801\u6587\u4ef6\u3002");
        }

        return true;
    }

    private async Task<string> FetchBoardDiagnosticsAsync()
    {
        if (_settings.RelayBaseUri is null)
        {
            throw new InvalidOperationException("Relay base URI is not configured.");
        }

        using RemoteRelayClient client = new(_settings.RelayBaseUri);
        return await client.GetDiagnosticsJsonAsync(_shutdown.Token);
    }

    private async Task<ProgramListResult> GetProgramListAsync()
    {
        if (_settings.RelayBaseUri is null)
        {
            throw new InvalidOperationException("Relay base URI is not configured.");
        }

        using RemoteRelayClient client = new(_settings.RelayBaseUri);
        return await client.GetProgramListAsync(_shutdown.Token);
    }

    private async Task<ProgramFileInfo> GetProgramFileInfoAsync(string fileName)
    {
        if (_settings.RelayBaseUri is null)
        {
            throw new InvalidOperationException("Relay base URI is not configured.");
        }

        using RemoteRelayClient client = new(_settings.RelayBaseUri);
        return await client.GetProgramFileInfoAsync(fileName, _shutdown.Token);
    }

    private async Task<ProgramFileContentResult> GetProgramFileContentAsync(string fileName)
    {
        if (_settings.RelayBaseUri is null)
        {
            throw new InvalidOperationException("Relay base URI is not configured.");
        }

        using RemoteRelayClient client = new(_settings.RelayBaseUri);
        return await client.GetProgramFileContentAsync(fileName, _shutdown.Token);
    }

    private async Task<ProgramDeleteResult> DeleteProgramFileAsync(string fileName)
    {
        if (_settings.RelayBaseUri is null)
        {
            throw new InvalidOperationException("Relay base URI is not configured.");
        }

        using RemoteRelayClient client = new(_settings.RelayBaseUri);
        return await client.DeleteProgramFileAsync(fileName, _shutdown.Token);
    }

    private async Task<ProgramUploadResult> UploadProgramAsync(string fileName, Stream stream, long length, string sha256, bool overwrite)
    {
        if (_settings.RelayBaseUri is null)
        {
            throw new InvalidOperationException("Relay base URI is not configured.");
        }

        using RemoteRelayClient client = new(_settings.RelayBaseUri);
        return await client.UploadProgramAsync(fileName, stream, length, sha256, overwrite, _shutdown.Token);
    }

    private static async Task<string> ComputeSha256HexAsync(Stream stream, CancellationToken cancellationToken)
    {
        byte[] hash = await SHA256.HashDataAsync(stream, cancellationToken);
        return Convert.ToHexString(hash).ToLowerInvariant();
    }

    private static string ShortSha(string value) =>
        value.Length <= 12 ? value : value[..12];

}
