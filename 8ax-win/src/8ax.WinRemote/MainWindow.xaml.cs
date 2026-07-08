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

public partial class MainWindow : Window
{
    private const double RelayMoveMinIntervalMs = 16.0;
    private const int RelayMoveMinPixelDelta = 1;
    private const int RelayReconnectInitialDelayMs = 700;
    private const int RelayReconnectMaxDelayMs = 5000;
    private const int SystemMetricsRefreshMs = 1000;
    private const int RelayStreamTargetFps = 30;
    private const double RelayFrameMetricsMinIntervalMs = 1000.0 / RelayStreamTargetFps;

    private readonly AppSettings _settings = AppSettings.LoadFromEnvironment();
    private readonly RemoteFramebuffer _relayFramebuffer = new();
    private readonly RemoteFrameAssembler _relayAssembler;
    private readonly FrameStats _stats = new();
    private readonly RuntimeEvidenceRecorder _evidence;
    private readonly WinRemoteUpdater _updater = new();
    private readonly CancellationTokenSource _shutdown = new();
    private readonly DispatcherTimer _systemMetricsTimer = new();
    private readonly string _relaySessionId = $"win-{Guid.NewGuid():N}";
    private RemoteRelayClient? _activeRelayClient;
    private bool _relayInputReady;
    private bool _relaySessionConnected;
    private bool _updateAvailable;
    private bool _awaitingRelayPointerFeedback;
    private bool _relayPointerIsDown;
    private bool _haveLastRelayPoint;
    private RemotePoint _lastRelayPoint;
    private bool _haveLastRelayMovePoint;
    private RemotePoint _lastRelayMovePoint;
    private DateTime _lastRelayFrameMetricsUtc = DateTime.MinValue;
    private string _connectionStateText = String.Empty;
    private string _connectionStateBackground = String.Empty;
    private string _connectionStateForeground = String.Empty;
    private long _relayPointerSeq;
    private int _relayMoveSendActive;
    private int _relayInputEnsureActive;
    private int _systemMetricsFetchActive;
    private DateTime _lastRelayPointerDownUtc;
    private DateTime _lastRelayMoveUtc = DateTime.MinValue;
    private bool _pointerCaptured;

    public MainWindow()
    {
        InitializeComponent();
        _evidence = new RuntimeEvidenceRecorder(_settings.EvidenceDirectory);
        _relayAssembler = new RemoteFrameAssembler(_relayFramebuffer);
        _evidence.RecordEvent("app_start", new Dictionary<string, object?>
        {
            ["mode"] = _settings.SourceMode.ToString(),
            ["evidence_dir"] = _settings.EvidenceDirectory,
            ["view_only"] = _settings.ViewOnly,
            ["enable_pointer"] = _settings.EnablePointer,
            ["enable_remote_input"] = _settings.EnableRemoteInput,
        });
        InputModeText.Text = PointerInputModeText;

        if (_settings.SourceMode != RemoteSourceMode.Relay || _settings.RelayBaseUri is null)
        {
            throw new InvalidOperationException("WinRemote requires relay stream mode; direct board framebuffer capture is retired.");
        }

        SourceText.Text = "relay stream";
        EmptyStateText.Text = $"Waiting for relay {_settings.RelayBaseUri}...";
        RemoteImage.Source = _relayFramebuffer.Bitmap;
        StatusText.Text = $"source: {_settings.RelayBaseUri}";
        UpdateRemotePointerStatusSlot();
        _evidence.RecordEvent("relay_mode_selected", new Dictionary<string, object?>
        {
            ["relay"] = _settings.RelayBaseUri,
            ["enable_remote_input"] = _settings.EnableRemoteInput,
        });
        _systemMetricsTimer.Interval = TimeSpan.FromMilliseconds(SystemMetricsRefreshMs);
        _systemMetricsTimer.Tick += async (_, _) => await RefreshSystemMetricsAsync();
        _systemMetricsTimer.Start();
        _ = RunRelayLoopAsync(_settings.RelayBaseUri);
        _ = CheckForVpsUpdateAsync();
    }

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

    private async Task RunRelayLoopAsync(Uri relayBaseUri)
    {
        int reconnectAttempt = 0;
        while (!_shutdown.IsCancellationRequested)
        {
            if (reconnectAttempt > 0 && !_relaySessionConnected)
            {
                await Dispatcher.InvokeAsync(() =>
                {
                    InputModeText.Text = PointerInputModeText;
                    PointerText.Text = $"relay reconnecting: attempt {reconnectAttempt}";
                    StatusText.Text = $"source: {relayBaseUri}  relay: reconnecting attempt {reconnectAttempt}  {RemotePointerStatusText}";
                });
            }

            using RemoteRelayClient relayClient = new(relayBaseUri);
            _activeRelayClient = relayClient;
            bool connectedBeforeEnd = false;
            try
            {
                connectedBeforeEnd = await RunRelaySessionAsync(relayBaseUri, relayClient, reconnectAttempt > 0);
                if (connectedBeforeEnd)
                {
                    reconnectAttempt = 0;
                }

                if (!_shutdown.IsCancellationRequested)
                {
                    _evidence.RecordEvent("relay_stream_ended", new Dictionary<string, object?>
                    {
                        ["relay"] = relayBaseUri,
                        ["attempt"] = reconnectAttempt,
                    });
                }
            }
            catch (OperationCanceledException) when (_shutdown.IsCancellationRequested)
            {
                break;
            }
            catch (Exception ex)
            {
                connectedBeforeEnd = _relaySessionConnected;
                await Dispatcher.InvokeAsync(() =>
                {
                    SetConnectionState("error", "#3B2730", "#FFB3B3");
                    _evidence.RecordEvent("relay_failed", new Dictionary<string, object?>
                    {
                        ["relay"] = relayBaseUri,
                        ["attempt"] = reconnectAttempt,
                        ["message"] = ex.Message,
                    });
                    StatusText.Text = $"source: {relayBaseUri}  relay failed: {Compact(ex.Message)}  {RemotePointerStatusText}";
                });
            }
            finally
            {
                if (ReferenceEquals(_activeRelayClient, relayClient))
                {
                    _activeRelayClient = null;
                }

                _relaySessionConnected = false;
                await Dispatcher.InvokeAsync(() => MarkRelayDisconnected());
            }

            if (connectedBeforeEnd)
            {
                reconnectAttempt = 0;
            }

            if (_shutdown.IsCancellationRequested)
            {
                break;
            }

            reconnectAttempt++;
            int delayMs = Math.Min(RelayReconnectMaxDelayMs, RelayReconnectInitialDelayMs * (1 << Math.Min(reconnectAttempt - 1, 3)));
            _evidence.RecordEvent("relay_reconnect_scheduled", new Dictionary<string, object?>
            {
                ["relay"] = relayBaseUri,
                ["attempt"] = reconnectAttempt,
                ["delay_ms"] = delayMs,
            });
            try
            {
                await Task.Delay(delayMs, _shutdown.Token);
            }
            catch (OperationCanceledException) when (_shutdown.IsCancellationRequested)
            {
                break;
            }
        }
    }

    private async Task<bool> RunRelaySessionAsync(Uri relayBaseUri, RemoteRelayClient relayClient, bool isReconnectAttempt)
    {
        if (!isReconnectAttempt)
        {
            SetConnectionState("connecting", "#28313D", "#D6E3EF");
        }

        _lastRelayFrameMetricsUtc = DateTime.MinValue;
        RemoteInfoMessage info = await relayClient.GetInfoAsync(_shutdown.Token);
        UpdateSystemMetrics(info.SystemMetrics);
        _evidence.RecordEvent("relay_info", new Dictionary<string, object?>
        {
            ["relay"] = relayBaseUri,
            ["width"] = info.Width,
            ["height"] = info.Height,
            ["pixel_format"] = info.PixelFormat,
            ["view_only"] = info.ViewOnly,
            ["system_metrics"] = FormatSystemMetrics(info.SystemMetrics),
            ["target_fps"] = RelayStreamTargetFps,
            ["target_frame_interval_ms"] = Math.Round(RelayFrameMetricsMinIntervalMs, 1),
        });
        if (_settings.EnableRemoteInput)
        {
            await EnableRelayInputAsync(relayClient, info);
        }

        _stats.MarkFullFrameRequest();
        _evidence.RecordEvent("full_frame_request", new Dictionary<string, object?>
        {
            ["reason"] = "initial",
            ["relay"] = relayBaseUri,
        });
        RemoteFramePacket fullFrame = await relayClient.GetFullFrameAsync(_shutdown.Token);
        ApplyRelayPacket(fullFrame, relayBaseUri, "full");
        _relaySessionConnected = true;

        try
        {
            await foreach (RemoteFramePacket packet in relayClient.ReadFrameStreamAsync(_shutdown.Token))
            {
                RemoteFrameApplyResult result = await Dispatcher.InvokeAsync(() => ApplyRelayPacket(packet, relayBaseUri, "stream"));
                if (result.Status == RemoteFrameApplyStatus.NeedFullFrame)
                {
                    _stats.MarkNeedFullFrame();
                    _evidence.RecordEvent("need_full_frame", new Dictionary<string, object?>
                    {
                        ["reason"] = result.Reason,
                        ["local_frame_id"] = result.FrameId,
                    });
                    StatusText.Text = $"source: {relayBaseUri}  relay: repairing frame {result.FrameId}  {RemotePointerStatusText}";
                    _stats.MarkFullFrameRepair();
                    _stats.MarkFullFrameRequest();
                    _evidence.RecordEvent("full_frame_request", new Dictionary<string, object?>
                    {
                        ["reason"] = "repair",
                        ["relay"] = relayBaseUri,
                    });
                    RemoteFramePacket repair = await relayClient.GetFullFrameAsync(_shutdown.Token);
                    await Dispatcher.InvokeAsync(() => ApplyRelayPacket(repair, relayBaseUri, "repair"));
                    await Dispatcher.InvokeAsync(() => ApplyRelayPacket(packet, relayBaseUri, "stream-retry"));
                }
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException || !_shutdown.IsCancellationRequested)
        {
            _evidence.RecordEvent("relay_stream_unavailable", new Dictionary<string, object?>
            {
                ["relay"] = relayBaseUri,
                ["message"] = ex.Message,
                ["action"] = "reconnect",
            });
            throw;
        }

        return true;
    }

    private void MarkRelayDisconnected()
    {
        _relayInputReady = false;
        _awaitingRelayPointerFeedback = false;
        _relayPointerIsDown = false;
        _haveLastRelayMovePoint = false;
        Interlocked.Exchange(ref _relayMoveSendActive, 0);
        if (_pointerCaptured)
        {
            _pointerCaptured = false;
            Viewport.ReleaseMouseCapture();
        }

        InputModeText.Text = PointerInputModeText;
        UpdateRemotePointerStatusSlot();
    }

    private async Task EnableRelayInputAsync(RemoteRelayClient relayClient, RemoteInfoMessage info)
    {
        if (info.ViewOnly)
        {
            await Dispatcher.InvokeAsync(() =>
            {
                _relayInputReady = false;
                InputModeText.Text = "remote input blocked";
                PointerText.Text = "relay is view-only";
                UpdateRemotePointerStatusSlot();
                _evidence.RecordEvent("relay_input_blocked", new Dictionary<string, object?>
                {
                    ["reason"] = "relay_view_only",
                    ["session_id"] = _relaySessionId,
                });
            });
            return;
        }

        try
        {
            using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(3));
            using CancellationTokenSource inputToken = CancellationTokenSource.CreateLinkedTokenSource(timeout.Token, _shutdown.Token);
            DateTime start = DateTime.UtcNow;
            PointerAckMessage grant = await relayClient.OpenInputAsync(_relaySessionId, inputToken.Token);
            double grantMs = (DateTime.UtcNow - start).TotalMilliseconds;
            await Dispatcher.InvokeAsync(() =>
            {
                _relayInputReady = true;
                InputModeText.Text = "relay input";
                PointerText.Text = "pointer: relay input ready";
                UpdateRemotePointerStatusSlot();
                _evidence.RecordEvent("relay_input_granted", new Dictionary<string, object?>
                {
                    ["session_id"] = _relaySessionId,
                    ["type"] = grant.Type,
                    ["grant_ms"] = Math.Round(grantMs, 1),
                });
            });
        }
        catch (Exception ex) when (ex is not OperationCanceledException || !_shutdown.IsCancellationRequested)
        {
            await Dispatcher.InvokeAsync(() =>
            {
                _relayInputReady = false;
                InputModeText.Text = "remote input failed";
                PointerText.Text = $"remote input failed: {Compact(ex.Message)}";
                UpdateRemotePointerStatusSlot();
                _evidence.RecordEvent("relay_input_failed", new Dictionary<string, object?>
                {
                    ["session_id"] = _relaySessionId,
                    ["message"] = ex.Message,
                });
            });
        }
    }

    private async Task<bool> EnsureRelayInputReadyAsync()
    {
        if (IsRelayPointerEnabled)
        {
            return true;
        }

        if (!IsRelayPointerConfigured)
        {
            return false;
        }

        RemoteRelayClient? client = _activeRelayClient;
        if (client is null)
        {
            PointerText.Text = "relay input not ready";
            return false;
        }

        if (Interlocked.Exchange(ref _relayInputEnsureActive, 1) == 1)
        {
            PointerText.Text = "relay input retrying";
            return false;
        }

        try
        {
            PointerText.Text = "relay input retrying";
            _evidence.RecordEvent("relay_input_retry_started", new Dictionary<string, object?>
            {
                ["session_id"] = _relaySessionId,
            });
            using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(2));
            using CancellationTokenSource retryToken = CancellationTokenSource.CreateLinkedTokenSource(timeout.Token, _shutdown.Token);
            RemoteInfoMessage info = await client.GetInfoAsync(retryToken.Token);
            UpdateSystemMetrics(info.SystemMetrics);
            await EnableRelayInputAsync(client, info);
            _evidence.RecordEvent(_relayInputReady ? "relay_input_retry_ready" : "relay_input_retry_blocked", new Dictionary<string, object?>
            {
                ["session_id"] = _relaySessionId,
                ["view_only"] = info.ViewOnly,
            });
            return _relayInputReady;
        }
        catch (Exception ex) when (ex is not OperationCanceledException || !_shutdown.IsCancellationRequested)
        {
            _relayInputReady = false;
            InputModeText.Text = "remote input failed";
            PointerText.Text = $"relay input retry failed: {Compact(ex.Message)}";
            UpdateRemotePointerStatusSlot();
            _evidence.RecordEvent("relay_input_retry_failed", new Dictionary<string, object?>
            {
                ["session_id"] = _relaySessionId,
                ["message"] = ex.Message,
            });
            return false;
        }
        finally
        {
            Interlocked.Exchange(ref _relayInputEnsureActive, 0);
        }
    }

    private RemoteFrameApplyResult ApplyRelayPacket(RemoteFramePacket packet, Uri relayBaseUri, string source)
    {
        RemoteFrameApplyResult result = _relayAssembler.Apply(packet);
        if (result.Status is RemoteFrameApplyStatus.AppliedFullFrame or RemoteFrameApplyStatus.AppliedDirtyRects)
        {
            bool recordFrameMetrics = ShouldRecordRelayFrameMetrics();
            if (result.Status == RemoteFrameApplyStatus.AppliedDirtyRects)
            {
                _stats.MarkDirtyRectFrame();
            }

            EmptyStateText.Visibility = Visibility.Collapsed;
            _stats.MarkFrame(ToStatsFrameId(result.FrameId), 0);
            SetConnectionState("live", "#263D30", "#A8E8B2");
            if (recordFrameMetrics)
            {
                _evidence.RecordEvent("frame_applied", new Dictionary<string, object?>
                {
                    ["source"] = source,
                    ["frame_id"] = result.FrameId,
                    ["status"] = result.Status.ToString(),
                    ["dirty_rects"] = result.DirtyRectCount,
                });
            }
            if (result.Status == RemoteFrameApplyStatus.AppliedDirtyRects && _awaitingRelayPointerFeedback)
            {
                _awaitingRelayPointerFeedback = false;
                double feedbackMs = (DateTime.UtcNow - _lastRelayPointerDownUtc).TotalMilliseconds;
                _evidence.RecordEvent("relay_pointer_feedback", new Dictionary<string, object?>
                {
                    ["feedback_ms"] = Math.Round(feedbackMs, 1),
                    ["frame_id"] = result.FrameId,
                    ["dirty_rects"] = result.DirtyRectCount,
                });
                PointerText.Text = $"relay feedback: {feedbackMs:0} ms";
            }
            if (recordFrameMetrics)
            {
                _evidence.RecordMetrics(_stats, "relay");
                UpdateRelayFrameStatus(relayBaseUri, source, result.DirtyRectCount);
            }
        }
        else if (result.Status == RemoteFrameApplyStatus.Rejected)
        {
            _stats.MarkRejectedFrame();
            SetConnectionState("error", "#3B2730", "#FFB3B3");
            _evidence.RecordEvent("frame_rejected", new Dictionary<string, object?>
            {
                ["frame_id"] = result.FrameId,
                ["reason"] = result.Reason,
            });
            _evidence.RecordMetrics(_stats, "relay");
            StatusRelayText.Text = "relay: reject";
            StatusDirtyText.Text = "dirty: --";
            StatusCountersText.Text = CountersText();
            StatusText.Text = $"source: {relayBaseUri}  rejected frame: {Compact(result.Reason)}";
            UpdateRemotePointerStatusSlot();
        }
        else if (result.Status == RemoteFrameApplyStatus.StaleFrame)
        {
            _evidence.RecordEvent("frame_stale", new Dictionary<string, object?>
            {
                ["frame_id"] = result.FrameId,
                ["reason"] = result.Reason,
                ["source"] = source,
            });
        }

        return result;
    }

    private static int ToStatsFrameId(long frameId) =>
        frameId > Int32.MaxValue ? Int32.MaxValue : Math.Max(0, (int)frameId);

    private bool ShouldRecordRelayFrameMetrics()
    {
        DateTime now = DateTime.UtcNow;
        if ((now - _lastRelayFrameMetricsUtc).TotalMilliseconds < RelayFrameMetricsMinIntervalMs)
        {
            return false;
        }

        _lastRelayFrameMetricsUtc = now;
        return true;
    }

    private string CountersText() =>
        $"full:{_stats.FullFrameRequests,4} dirty:{_stats.DirtyRectFrames,4} repair:{_stats.FullFrameRepairs,4} reject:{_stats.RejectedFrames,3}";

    private void UpdateRelayFrameStatus(Uri relayBaseUri, string source, int dirtyRectCount)
    {
        StatusFrameText.Text = $"frame: {_stats.FrameId:000000}";
        StatusFpsText.Text = $"fps: {_stats.Fps,5:0.0}";
        StatusRelayText.Text = $"relay: {source}";
        StatusDirtyText.Text = $"dirty:{dirtyRectCount,3}";
        StatusCountersText.Text = CountersText();
        StatusText.Text = $"source: {relayBaseUri}";
        UpdateRemotePointerStatusSlot();
    }

    private void UpdateRemotePointerStatusSlot()
    {
        StatusRemotePointerText.Text = RemotePointerStatusText;
    }

    private async Task RefreshSystemMetricsAsync()
    {
        RemoteRelayClient? client = _activeRelayClient;
        if (client is null)
        {
            UpdateSystemMetrics(null);
            return;
        }

        if (Interlocked.Exchange(ref _systemMetricsFetchActive, 1) == 1)
        {
            return;
        }

        try
        {
            using CancellationTokenSource timeout = new(TimeSpan.FromMilliseconds(SystemMetricsRefreshMs));
            using CancellationTokenSource linked = CancellationTokenSource.CreateLinkedTokenSource(timeout.Token, _shutdown.Token);
            RemoteInfoMessage info = await client.GetInfoAsync(linked.Token);
            UpdateSystemMetrics(info.SystemMetrics);
        }
        catch (Exception ex) when (ex is not OperationCanceledException || !_shutdown.IsCancellationRequested)
        {
            SystemMetricsText.ToolTip = $"system metrics unavailable: {Compact(ex.Message)}";
        }
        finally
        {
            Interlocked.Exchange(ref _systemMetricsFetchActive, 0);
        }
    }

    private void UpdateSystemMetrics(RemoteSystemMetrics? metrics)
    {
        Cpu0MetricValue.Text = Percent(metrics?.Cpu0Percent);
        Cpu1MetricValue.Text = Percent(metrics?.Cpu1Percent);
        MemoryMetricValue.Text = Percent(metrics?.MemoryPercent);
        DiskMetricValue.Text = Percent(metrics?.DiskPercent);
        SystemMetricsText.ToolTip = metrics is null
            ? "waiting for board /remote/info system_metrics"
            : FormatSystemMetricsTooltip(metrics);
    }

    private static string MissingSystemMetricsText() =>
        "cpu0 --%  cpu1 --%  内存 --%  硬盘 --%";

    private static string FormatSystemMetrics(RemoteSystemMetrics? metrics)
    {
        if (metrics is null)
        {
            return MissingSystemMetricsText();
        }

        return $"cpu0 {Percent(metrics.Cpu0Percent)}  cpu1 {Percent(metrics.Cpu1Percent)}  内存 {Percent(metrics.MemoryPercent)}  硬盘 {Percent(metrics.DiskPercent)}";
    }

    private static string FormatSystemMetricsTooltip(RemoteSystemMetrics metrics) =>
        $"cpu0 {Percent(metrics.Cpu0Percent)} / cpu1 {Percent(metrics.Cpu1Percent)} / memory {BytesPair(metrics.MemoryUsedBytes, metrics.MemoryTotalBytes)} / disk {BytesPair(metrics.DiskUsedBytes, metrics.DiskTotalBytes)}";

    private static string Percent(double? value) =>
        value.HasValue ? $"{Math.Clamp(value.Value, 0.0, 100.0):0}%" : "--%";

    private static string BytesPair(long? used, long? total) =>
        used.HasValue && total.HasValue ? $"{FormatBytes(used.Value)} / {FormatBytes(total.Value)}" : "--";

    private static string FormatBytes(long bytes)
    {
        string[] units = new[] { "B", "KB", "MB", "GB", "TB" };
        double value = Math.Max(0L, bytes);
        int unit = 0;
        while (value >= 1024.0 && unit < units.Length - 1)
        {
            value /= 1024.0;
            unit++;
        }

        return $"{value:0.#}{units[unit]}";
    }

    private bool IsRelayPointerConfigured =>
        _settings.SourceMode == RemoteSourceMode.Relay && _settings.EnableRemoteInput;

    private bool IsRelayPointerEnabled =>
        IsRelayPointerConfigured && _relayInputReady;

    private bool IsPointerEnabled =>
        IsRelayPointerEnabled;

    private string RemotePointerStatusText =>
        IsRelayPointerEnabled ? "remote pointer: relay" :
        IsRelayPointerConfigured ? "remote pointer: waiting" :
        "remote pointer: disabled";

    private string PointerInputModeText =>
        IsRelayPointerEnabled ? "input:relay" :
        IsRelayPointerConfigured ? "input:relay waiting" :
        "input:view-only";

    private void SetConnectionState(string text, string background, string foreground)
    {
        if (String.Equals(_connectionStateText, text, StringComparison.Ordinal)
            && String.Equals(_connectionStateBackground, background, StringComparison.Ordinal)
            && String.Equals(_connectionStateForeground, foreground, StringComparison.Ordinal))
        {
            return;
        }

        _connectionStateText = text;
        _connectionStateBackground = background;
        _connectionStateForeground = foreground;
        _evidence.RecordEvent("connection_state_changed", new Dictionary<string, object?>
        {
            ["state"] = text,
        });
        ConnectionBadge.Background = (Brush)new BrushConverter().ConvertFromString(background)!;
        ConnectionText.Foreground = (Brush)new BrushConverter().ConvertFromString(foreground)!;
        ConnectionText.Text = text;
    }

    private static string Compact(string text)
    {
        text = text.Replace("\r", " ").Replace("\n", " ").Trim();
        return text.Length <= 140 ? text : text[..140] + "...";
    }

    private async void Viewport_OnMouseMove(object sender, MouseEventArgs e)
    {
        Point p = e.GetPosition(Viewport);
        RemotePoint remote = PointerMapper.Map(p, new Size(Viewport.ActualWidth, Viewport.ActualHeight));
        if (_pointerCaptured && IsRelayPointerEnabled && _relayPointerIsDown)
        {
            e.Handled = true;
            if (!remote.IsInside)
            {
                PointerText.Text = "drag outside remote frame";
                return;
            }

            _lastRelayPoint = remote;
            _haveLastRelayPoint = true;
            PointerText.Text = $"x:{remote.X,4} y:{remote.Y,3}  input:relay move";
            if (ShouldSendRelayMove(remote))
            {
                await SendRelayMoveAsync(remote);
            }

            return;
        }

        PointerText.Text = remote.IsInside
            ? $"x:{remote.X,4} y:{remote.Y,3}  {PointerInputModeText}"
            : "outside remote frame";
    }

    private async void Viewport_OnMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (!IsPointerEnabled)
        {
            bool enabled = await EnsureRelayInputReadyAsync();
            if (!enabled)
            {
                return;
            }
        }

        RemotePoint remote = PointerMapper.Map(e.GetPosition(Viewport), new Size(Viewport.ActualWidth, Viewport.ActualHeight));
        if (!remote.IsInside)
        {
            return;
        }

        _pointerCaptured = true;
        Viewport.CaptureMouse();
        if (IsRelayPointerEnabled)
        {
            _relayPointerIsDown = true;
            _lastRelayPoint = remote;
            _haveLastRelayPoint = true;
            _lastRelayMovePoint = remote;
            _haveLastRelayMovePoint = true;
            _lastRelayMoveUtc = DateTime.MinValue;
            PointerText.Text = $"x:{remote.X,4} y:{remote.Y,3}  input:relay down";
            e.Handled = true;
            await SendRelayPointerAsync("down", remote);
            return;
        }

        PointerText.Text = "input:view-only";
    }

    private async void Viewport_OnMouseLeftButtonUp(object sender, MouseButtonEventArgs e)
    {
        bool wasCaptured = _pointerCaptured;
        _pointerCaptured = false;
        if (wasCaptured)
        {
            Viewport.ReleaseMouseCapture();
        }

        if (!IsPointerEnabled)
        {
            return;
        }

        RemotePoint remote = PointerMapper.Map(e.GetPosition(Viewport), new Size(Viewport.ActualWidth, Viewport.ActualHeight));
        if (!remote.IsInside && !(IsRelayPointerEnabled && wasCaptured && _haveLastRelayPoint))
        {
            return;
        }

        e.Handled = true;
        if (IsRelayPointerEnabled)
        {
            RemotePoint relayPoint = remote.IsInside ? remote : _lastRelayPoint;
            _relayPointerIsDown = false;
            _haveLastRelayMovePoint = false;
            await SendRelayPointerAsync("up", relayPoint);
            return;
        }

        PointerText.Text = "input:view-only";
    }

    private void Viewport_OnMouseLeave(object sender, MouseEventArgs e)
    {
        if (_pointerCaptured)
        {
            return;
        }

        PointerText.Text = IsPointerEnabled ? "pointer: relay input ready" : "pointer: disabled";
    }

    private bool ShouldSendRelayMove(RemotePoint remote)
    {
        DateTime now = DateTime.UtcNow;
        if (_haveLastRelayMovePoint)
        {
            int dx = Math.Abs(remote.X - _lastRelayMovePoint.X);
            int dy = Math.Abs(remote.Y - _lastRelayMovePoint.Y);
            if (dx < RelayMoveMinPixelDelta && dy < RelayMoveMinPixelDelta)
            {
                return false;
            }

            if ((now - _lastRelayMoveUtc).TotalMilliseconds < RelayMoveMinIntervalMs)
            {
                return false;
            }
        }

        _lastRelayMovePoint = remote;
        _haveLastRelayMovePoint = true;
        _lastRelayMoveUtc = now;
        return true;
    }

    private async Task SendRelayMoveAsync(RemotePoint remote)
    {
        if (Interlocked.Exchange(ref _relayMoveSendActive, 1) == 1)
        {
            return;
        }

        try
        {
            await SendRelayPointerAsync("move", remote);
        }
        finally
        {
            Interlocked.Exchange(ref _relayMoveSendActive, 0);
        }
    }

    private async Task SendRelayPointerAsync(string phase, RemotePoint remote, bool allowReconnectRetry = true)
    {
        RemoteRelayClient? client = _activeRelayClient;
        if (client is null || !_relayInputReady)
        {
            if (String.Equals(phase, "down", StringComparison.OrdinalIgnoreCase))
            {
                _relayPointerIsDown = false;
            }

            PointerText.Text = "relay input not ready";
            return;
        }

        long seq = Interlocked.Increment(ref _relayPointerSeq);
        long clientTimeMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        PointerEventMessage message = new(
            RemoteMessageTypes.PointerEvent,
            _relaySessionId,
            "8ax-win",
            seq,
            phase,
            remote.X,
            remote.Y,
            "left",
            clientTimeMs);

        if (String.Equals(phase, "down", StringComparison.OrdinalIgnoreCase))
        {
            _lastRelayPointerDownUtc = DateTime.UtcNow;
            _awaitingRelayPointerFeedback = true;
        }

        DateTime sendStart = DateTime.UtcNow;
        _evidence.RecordEvent("relay_pointer_sent", new Dictionary<string, object?>
        {
            ["session_id"] = _relaySessionId,
            ["seq"] = seq,
            ["phase"] = phase,
            ["x"] = remote.X,
            ["y"] = remote.Y,
        });

        try
        {
            using CancellationTokenSource timeout = new(TimeSpan.FromMilliseconds(800));
            using CancellationTokenSource inputToken = CancellationTokenSource.CreateLinkedTokenSource(timeout.Token, _shutdown.Token);
            PointerAckMessage ack = await client.SendPointerEventAsync(message, inputToken.Token);
            double ackMs = (DateTime.UtcNow - sendStart).TotalMilliseconds;
            PointerText.Text = $"x:{remote.X,4} y:{remote.Y,3}  relay {phase} ack:{ackMs:0}ms";
            _evidence.RecordEvent("relay_pointer_ack", new Dictionary<string, object?>
            {
                ["session_id"] = _relaySessionId,
                ["seq"] = seq,
                ["phase"] = phase,
                ["ack_ms"] = Math.Round(ackMs, 1),
                ["accepted"] = ack.Accepted,
                ["server_time_ms"] = ack.ServerTimeMs,
            });
        }
        catch (Exception ex) when (ex is not OperationCanceledException || !_shutdown.IsCancellationRequested)
        {
            _relayInputReady = false;
            InputModeText.Text = PointerInputModeText;
            UpdateRemotePointerStatusSlot();
            _evidence.RecordEvent("relay_pointer_failed", new Dictionary<string, object?>
            {
                ["session_id"] = _relaySessionId,
                ["seq"] = seq,
                ["phase"] = phase,
                ["x"] = remote.X,
                ["y"] = remote.Y,
                ["message"] = ex.Message,
            });

            if (allowReconnectRetry && String.Equals(phase, "down", StringComparison.OrdinalIgnoreCase))
            {
                PointerText.Text = "relay input reconnecting";
                _evidence.RecordEvent("relay_pointer_reconnect_retry_started", new Dictionary<string, object?>
                {
                    ["session_id"] = _relaySessionId,
                    ["failed_seq"] = seq,
                    ["phase"] = phase,
                    ["x"] = remote.X,
                    ["y"] = remote.Y,
                });
                if (await EnsureRelayInputReadyAsync())
                {
                    _relayPointerIsDown = true;
                    _lastRelayPoint = remote;
                    _haveLastRelayPoint = true;
                    await SendRelayPointerAsync(phase, remote, allowReconnectRetry: false);
                    return;
                }
            }

            if (String.Equals(phase, "down", StringComparison.OrdinalIgnoreCase))
            {
                _awaitingRelayPointerFeedback = false;
                _relayPointerIsDown = false;
            }
            PointerText.Text = $"relay input failed: {Compact(ex.Message)}";
        }
    }

    protected override void OnClosed(EventArgs e)
    {
        _shutdown.Cancel();
        _systemMetricsTimer.Stop();
        _evidence.RecordEvent("app_closed");
        _updater.Dispose();
        _shutdown.Dispose();
        base.OnClosed(e);
    }
}
