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
    private const double RelayEvidenceIntervalMs = 1000.0;
    private const double RelayStatusIntervalMs = 100.0;
    private DateTime _lastRelayFrameStatusUtc = DateTime.MinValue;

    private RemoteFrameApplyResult ApplyRelayPacket(RemoteFramePacket packet, Uri relayBaseUri, string source)
    {
        RemoteFrameApplyResult result = _relayAssembler.Apply(packet);
        if (result.Status is RemoteFrameApplyStatus.AppliedFullFrame or RemoteFrameApplyStatus.AppliedDirtyRects)
        {
            DateTime now = DateTime.UtcNow;
            bool recordFrameEvidence = ShouldRecordRelayFrameEvidence(now);
            bool refreshFrameStatus = ShouldRefreshRelayFrameStatus(now);
            if (result.Status == RemoteFrameApplyStatus.AppliedDirtyRects)
            {
                _stats.MarkDirtyRectFrame();
            }

            EmptyStateText.Visibility = Visibility.Collapsed;
            _stats.MarkFrame(ToStatsFrameId(result.FrameId), 0);
            SetConnectionState("live", "#263D30", "#A8E8B2");
            if (recordFrameEvidence)
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
            if (recordFrameEvidence)
            {
                _evidence.RecordMetrics(_stats, "relay");
            }
            if (refreshFrameStatus)
            {
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

    private bool ShouldRecordRelayFrameEvidence(DateTime now)
    {
        if ((now - _lastRelayFrameMetricsUtc).TotalMilliseconds < RelayEvidenceIntervalMs)
        {
            return false;
        }

        _lastRelayFrameMetricsUtc = now;
        return true;
    }

    private bool ShouldRefreshRelayFrameStatus(DateTime now)
    {
        if ((now - _lastRelayFrameStatusUtc).TotalMilliseconds < RelayStatusIntervalMs)
        {
            return false;
        }

        _lastRelayFrameStatusUtc = now;
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

}
