using System.Diagnostics;
using System.IO;
using System.Text.Json;

namespace EightAxis.WinRemote.Diagnostics;

public sealed class FrameStats
{
    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private long _lastTicks;
    private double _fps;

    public int FrameId { get; private set; }

    public double Fps => _fps;

    public int LatencyMs { get; private set; }

    public int FullFrameRequests { get; private set; }

    public int DirtyRectFrames { get; private set; }

    public int FullFrameRepairs { get; private set; }

    public int NeedFullFrameCount { get; private set; }

    public int RejectedFrames { get; private set; }

    public void MarkFrame(int frameId, int captureElapsedMs)
    {
        FrameId = frameId;
        long nowTicks = _clock.ElapsedTicks;
        if (_lastTicks > 0)
        {
            double seconds = (nowTicks - _lastTicks) / (double)Stopwatch.Frequency;
            if (seconds > 0)
            {
                double instantFps = 1.0 / seconds;
                _fps = _fps <= 0 ? instantFps : (_fps * 0.85) + (instantFps * 0.15);
            }
        }

        _lastTicks = nowTicks;
        LatencyMs = Math.Max(0, captureElapsedMs);
    }

    public void MarkFullFrameRequest() => FullFrameRequests++;

    public void MarkDirtyRectFrame() => DirtyRectFrames++;

    public void MarkFullFrameRepair() => FullFrameRepairs++;

    public void MarkNeedFullFrame() => NeedFullFrameCount++;

    public void MarkRejectedFrame() => RejectedFrames++;
}

public sealed class RuntimeEvidenceRecorder
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = false };
    private readonly object _sync = new();

    public RuntimeEvidenceRecorder(string outputDirectory)
    {
        OutputDirectory = outputDirectory;
        Directory.CreateDirectory(OutputDirectory);
        EventsPath = Path.Combine(OutputDirectory, "win_client_events.jsonl");
        MetricsPath = Path.Combine(OutputDirectory, "win_client_metrics.jsonl");
    }

    public string OutputDirectory { get; }

    public string EventsPath { get; }

    public string MetricsPath { get; }

    public void RecordEvent(string name, IReadOnlyDictionary<string, object?>? fields = null)
    {
        Dictionary<string, object?> row = CreateBaseRow();
        row["event"] = name;
        AddFields(row, fields);
        AppendJsonLine(EventsPath, row);
    }

    public void RecordMetrics(FrameStats stats, string source)
    {
        Dictionary<string, object?> row = CreateBaseRow();
        row["source"] = SanitizeValue("source", source);
        row["frame_id"] = stats.FrameId;
        row["fps"] = Math.Round(stats.Fps, 3);
        row["latency_ms"] = stats.LatencyMs;
        row["full_frame_requests"] = stats.FullFrameRequests;
        row["dirty_rect_frames"] = stats.DirtyRectFrames;
        row["full_frame_repairs"] = stats.FullFrameRepairs;
        row["need_full_frame"] = stats.NeedFullFrameCount;
        row["rejected_frames"] = stats.RejectedFrames;
        AppendJsonLine(MetricsPath, row);
    }

    private static Dictionary<string, object?> CreateBaseRow() => new()
    {
        ["ts_utc"] = DateTimeOffset.UtcNow.ToString("O"),
    };

    private static void AddFields(Dictionary<string, object?> row, IReadOnlyDictionary<string, object?>? fields)
    {
        if (fields is null)
        {
            return;
        }

        foreach (KeyValuePair<string, object?> field in fields)
        {
            row[field.Key] = SanitizeValue(field.Key, field.Value);
        }
    }

    private static object? SanitizeValue(string key, object? value)
    {
        if (IsSensitiveKey(key))
        {
            return "[redacted]";
        }

        if (value is Uri uri)
        {
            return uri.GetLeftPart(UriPartial.Path);
        }

        if (value is string text && LooksLikeSensitiveText(text))
        {
            return "[redacted]";
        }

        return value;
    }

    private static bool IsSensitiveKey(string key) =>
        key.Contains("token", StringComparison.OrdinalIgnoreCase)
        || key.Contains("secret", StringComparison.OrdinalIgnoreCase)
        || key.Contains("password", StringComparison.OrdinalIgnoreCase)
        || key.Contains("authorization", StringComparison.OrdinalIgnoreCase);

    private static bool LooksLikeSensitiveText(string text) =>
        text.Contains("token=", StringComparison.OrdinalIgnoreCase)
        || text.Contains("authorization:", StringComparison.OrdinalIgnoreCase)
        || text.Contains("bearer ", StringComparison.OrdinalIgnoreCase);

    private void AppendJsonLine(string path, IReadOnlyDictionary<string, object?> row)
    {
        string json = JsonSerializer.Serialize(row, JsonOptions);
        lock (_sync)
        {
            File.AppendAllText(path, json + Environment.NewLine);
        }
    }
}
