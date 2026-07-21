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
        _lastRelayFrameStatusUtc = DateTime.MinValue;
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

}
