using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Globalization;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace EightAxis.WinRemote.Diagnostics;

public sealed class ClientLogPreviewDialog : Window
{
    private static readonly Regex SensitiveDiagnosticsTokenPattern = new(
        "v3_status_shm|phase0_bus5|linuxcnc|ethercat|hardware|firmware|tool\\.tbl|/opt/8ax|/dev|/proc|/sys|cpu[0-9]*|memory|disk|shm|hal|wcs|g92|rtcp|joint|axis|drive|servo|motor|encoder",
        RegexOptions.Compiled | RegexOptions.CultureInvariant | RegexOptions.IgnoreCase);

    private readonly Func<Task<string>>? _refreshAsync;
    private readonly TextBlock _headerText = new();
    private readonly TextBox _logText = new();
    private readonly Button _refreshButton;
    private readonly Button _detailsButton;
    private string _currentBody = String.Empty;
    private bool _showDetails;

    public ClientLogPreviewDialog(string title, string header, string body, Func<Task<string>>? refreshAsync = null)
    {
        _refreshAsync = refreshAsync;
        Title = title;
        Width = 980;
        Height = 700;
        MinWidth = 760;
        MinHeight = 520;
        Background = BrushFrom("#111318");
        Foreground = BrushFrom("#D6E3EF");
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        UseLayoutRounding = true;

        Grid root = new();
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        Content = root;

        _headerText.Margin = new Thickness(16, 14, 16, 8);
        _headerText.Foreground = BrushFrom("#B7C1CC");
        _headerText.FontFamily = new FontFamily("Consolas");
        _headerText.FontSize = 13;
        _headerText.Text = SanitizeDiagnosticsText(header);
        _headerText.TextWrapping = TextWrapping.Wrap;
        root.Children.Add(_headerText);

        _logText.Margin = new Thickness(16, 0, 16, 12);
        _logText.Padding = new Thickness(10);
        _logText.Background = BrushFrom("#05070A");
        _logText.Foreground = BrushFrom("#D6E3EF");
        _logText.BorderBrush = BrushFrom("#39414D");
        _logText.FontFamily = new FontFamily("Consolas");
        _logText.FontSize = 12;
        _logText.IsReadOnly = true;
        _logText.AcceptsReturn = true;
        _logText.AcceptsTab = true;
        _logText.TextWrapping = TextWrapping.Wrap;
        _logText.VerticalScrollBarVisibility = ScrollBarVisibility.Auto;
        _logText.HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled;
        Grid.SetRow(_logText, 1);
        root.Children.Add(_logText);

        StackPanel buttons = new()
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right,
            Margin = new Thickness(16, 0, 16, 14),
        };
        Grid.SetRow(buttons, 2);
        root.Children.Add(buttons);

        _refreshButton = CreateDialogButton("\u5237\u65b0");
        _refreshButton.Click += async (_, _) => await RefreshAsync();
        buttons.Children.Add(_refreshButton);

        _detailsButton = CreateDialogButton("\u8be6\u7ec6\u4fe1\u606f");
        _detailsButton.Margin = new Thickness(10, 0, 0, 0);
        _detailsButton.Click += (_, _) => ToggleDetails();
        buttons.Children.Add(_detailsButton);

        Button closeButton = CreateDialogButton("\u5173\u95ed");
        closeButton.Margin = new Thickness(10, 0, 0, 0);
        closeButton.Click += (_, _) => Close();
        buttons.Children.Add(closeButton);

        SetBody(body);
    }

    private async Task RefreshAsync()
    {
        if (_refreshAsync is null)
        {
            return;
        }

        _refreshButton.IsEnabled = false;
        try
        {
            SetBody(await _refreshAsync().ConfigureAwait(true));
        }
        catch (Exception ex)
        {
            SetBody($"refresh failed: {ex.Message}");
        }
        finally
        {
            _refreshButton.IsEnabled = true;
        }
    }

    private void SetBody(string body)
    {
        _currentBody = body;
        _logText.Text = BuildDisplayText(body, _showDetails);
        _logText.CaretIndex = _logText.Text.Length;
        _logText.ScrollToEnd();
    }

    private void ToggleDetails()
    {
        _showDetails = !_showDetails;
        _detailsButton.Content = _showDetails ? "\u6458\u8981" : "\u8be6\u7ec6\u4fe1\u606f";
        _logText.Text = BuildDisplayText(_currentBody, _showDetails);
        _logText.CaretIndex = _showDetails ? 0 : _logText.Text.Length;
        if (_showDetails)
        {
            _logText.ScrollToHome();
        }
        else
        {
            _logText.ScrollToEnd();
        }
    }

    private static string BuildDisplayText(string body, bool showDetails) =>
        showDetails ? FormatDetailedDisplayText(body) : BuildSummaryDisplayText(body);

    private static string FormatDetailedDisplayText(string body)
    {
        if (String.IsNullOrWhiteSpace(body))
        {
            return SanitizeDiagnosticsText(body);
        }

        string trimmed = body.TrimStart();
        if (!trimmed.StartsWith("{", StringComparison.Ordinal) && !trimmed.StartsWith("[", StringComparison.Ordinal))
        {
            return SanitizeDiagnosticsText(body);
        }

        try
        {
            using JsonDocument document = JsonDocument.Parse(body);
            string formatted = JsonSerializer.Serialize(
                document.RootElement,
                new JsonSerializerOptions
                {
                    Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
                    WriteIndented = true,
                });
            return SanitizeDiagnosticsText(formatted);
        }
        catch (JsonException)
        {
            return SanitizeDiagnosticsText(body);
        }
    }

    private static string BuildSummaryDisplayText(string body)
    {
        if (String.IsNullOrWhiteSpace(body))
        {
            return "\u8bca\u65ad\u6458\u8981\uff1a\u65e0\u6570\u636e";
        }

        try
        {
            using JsonDocument document = JsonDocument.Parse(body);
            JsonElement root = document.RootElement;
            StringBuilder summary = new();
            summary.AppendLine("\u8bca\u65ad\u6458\u8981");
            summary.AppendLine();
            summary.AppendLine($"\u8bca\u65ad\u65f6\u95f4\uff1a{FormatUnixMs(GetLong(root, "collected_at_unix_ms"))}");
            summary.AppendLine($"\u8bca\u65ad\u7f16\u53f7\uff1a{ShortId(body)}");
            summary.AppendLine($"\u603b\u4f53\u72b6\u6001\uff1a{OverallStatus(root)}");
            summary.AppendLine();
            summary.AppendLine("\u5173\u952e\u72b6\u6001");
            summary.AppendLine($"\u754c\u9762\u670d\u52a1\uff1a{RunFileStatus(root, "remote_info.json")}");
            summary.AppendLine($"\u8fdc\u7a0b\u663e\u793a\uff1a{DisplayStatus(root)}");
            summary.AppendLine($"\u8fdc\u7a0b\u8f93\u5165\uff1a{InputStatus(root)}");
            summary.AppendLine($"\u72b6\u6001\u540c\u6b65\uff1a{AnyRunFileStatus(root, "state_shm_shadow_reader_status.json")}");
            summary.AppendLine($"\u6307\u4ee4\u901a\u9053\uff1a{AnyRunFileStatus(root, "command_broker_status.json")}");
            summary.AppendLine($"\u7a0b\u5e8f\u6587\u4ef6\uff1a{ProgramFileStatus(root)}");
            summary.AppendLine();
            summary.AppendLine("\u6700\u8fd1\u60c5\u51b5");
            summary.AppendLine($"\u753b\u9762\u5237\u65b0\uff1a{FrameRefreshStatus(root)}");
            summary.AppendLine($"\u8f93\u5165\u72b6\u6001\uff1a{RecentInputStatus(root)}");
            summary.AppendLine($"\u6700\u8fd1\u9519\u8bef\uff1a{RecentErrorStatus(root)}");
            summary.AppendLine();
            summary.AppendLine("\u8be6\u7ec6\u4fe1\u606f\u5df2\u8131\u654f\uff0c\u70b9\u51fb\u201c\u8be6\u7ec6\u4fe1\u606f\u201d\u67e5\u770b\u3002");
            return SanitizeDiagnosticsText(summary.ToString());
        }
        catch (JsonException)
        {
            return SanitizeDiagnosticsText(body);
        }
    }

    private static string OverallStatus(JsonElement root)
    {
        bool hasInfo = root.TryGetProperty("info", out _);
        bool hasRelay = root.TryGetProperty("relay", out _);
        return hasInfo && hasRelay ? "\u6b63\u5e38" : "\u4fe1\u606f\u4e0d\u5b8c\u6574";
    }

    private static string DisplayStatus(JsonElement root)
    {
        long frameId = 0;
        if (root.TryGetProperty("relay", out JsonElement relay))
        {
            frameId = GetLong(relay, "latest_frame_id") ?? 0;
        }

        return frameId > 0 ? "\u6b63\u5e38\u5237\u65b0" : "\u672a\u89c1\u5237\u65b0";
    }

    private static string InputStatus(JsonElement root)
    {
        if (root.TryGetProperty("relay", out JsonElement relay) && TryGetBool(relay, "input_enabled", out bool enabled))
        {
            return enabled ? "\u53ef\u7528" : "\u672a\u5c31\u7eea";
        }

        return "\u672a\u77e5";
    }

    private static string FrameRefreshStatus(JsonElement root)
    {
        if (!root.TryGetProperty("relay", out JsonElement relay))
        {
            return "\u672a\u77e5";
        }

        int count = 0;
        if (relay.TryGetProperty("dirty_events", out JsonElement events) && events.ValueKind == JsonValueKind.Array)
        {
            count = events.GetArrayLength();
        }

        return count > 0 ? "\u6301\u7eed" : "\u6682\u65e0\u53d8\u5316";
    }

    private static string RecentInputStatus(JsonElement root)
    {
        if (root.TryGetProperty("relay", out JsonElement relay) && TryGetBool(relay, "recent_input_active", out bool active))
        {
            return active ? "\u521a\u6709\u64cd\u4f5c" : "\u6700\u8fd1\u65e0\u64cd\u4f5c";
        }

        return "\u672a\u77e5";
    }

    private static string RecentErrorStatus(JsonElement root)
    {
        if (!root.TryGetProperty("run_files", out JsonElement files) || files.ValueKind != JsonValueKind.Object)
        {
            return "\u65e0\u660e\u663e\u9519\u8bef";
        }

        foreach (JsonProperty file in files.EnumerateObject())
        {
            if (file.Value.TryGetProperty("error", out JsonElement error) && !String.IsNullOrWhiteSpace(error.GetString()))
            {
                return "\u6709\u5f02\u5e38\u6458\u8981\uff08\u8be6\u7ec6\u4fe1\u606f\u5df2\u8131\u654f\uff09";
            }
        }

        return "\u65e0\u660e\u663e\u9519\u8bef";
    }

    private static string ProgramFileStatus(JsonElement root)
    {
        long? maxBytes = GetLong(root, "max_upload_bytes");
        return maxBytes is > 0 ? "\u53ef\u4e0a\u4f20" : "\u672a\u77e5";
    }

    private static string RunFileStatus(JsonElement root, string name)
    {
        if (!TryGetRunFile(root, name, out JsonElement file))
        {
            return "\u672a\u89c1\u72b6\u6001";
        }

        return TryGetBool(file, "exists", out bool exists) && exists ? "\u6b63\u5e38" : "\u672a\u89c1\u72b6\u6001";
    }

    private static string AnyRunFileStatus(JsonElement root, params string[] names)
    {
        foreach (string name in names)
        {
            if (RunFileStatus(root, name) == "\u6b63\u5e38")
            {
                return "\u6b63\u5e38";
            }
        }

        return "\u672a\u89c1\u72b6\u6001";
    }

    private static bool TryGetRunFile(JsonElement root, string name, out JsonElement file)
    {
        file = default;
        return root.TryGetProperty("run_files", out JsonElement files)
            && files.ValueKind == JsonValueKind.Object
            && files.TryGetProperty(name, out file);
    }

    private static long? GetLong(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out JsonElement value))
        {
            return null;
        }

        if (value.ValueKind == JsonValueKind.Number && value.TryGetInt64(out long result))
        {
            return result;
        }

        return null;
    }

    private static bool TryGetBool(JsonElement element, string name, out bool result)
    {
        result = false;
        if (!element.TryGetProperty(name, out JsonElement value))
        {
            return false;
        }

        if (value.ValueKind == JsonValueKind.True || value.ValueKind == JsonValueKind.False)
        {
            result = value.GetBoolean();
            return true;
        }

        return false;
    }

    private static string FormatUnixMs(long? unixMs)
    {
        if (unixMs is null or <= 0)
        {
            return "\u672a\u77e5";
        }

        return DateTimeOffset.FromUnixTimeMilliseconds(unixMs.Value)
            .ToLocalTime()
            .ToString("yyyy-MM-dd HH:mm:ss zzz", CultureInfo.InvariantCulture);
    }

    private static string ShortId(string body)
    {
        int hash = StringComparer.Ordinal.GetHashCode(body);
        return $"diag-{Math.Abs(hash):x8}";
    }

    private static string SanitizeDiagnosticsText(string value) =>
        SensitiveDiagnosticsTokenPattern.Replace(value, "[redacted]");

    private static Button CreateDialogButton(string text) => new()
    {
        Content = text,
        Width = 92,
        Height = 30,
        Padding = new Thickness(12, 4, 12, 4),
        BorderBrush = BrushFrom("#53657A"),
        Background = BrushFrom("#202733"),
        Foreground = BrushFrom("#D6E3EF"),
        Cursor = System.Windows.Input.Cursors.Hand,
    };

    private static Brush BrushFrom(string value) =>
        (Brush)new BrushConverter().ConvertFromString(value)!;
}
