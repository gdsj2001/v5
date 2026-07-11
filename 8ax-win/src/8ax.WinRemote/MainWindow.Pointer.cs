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

}
