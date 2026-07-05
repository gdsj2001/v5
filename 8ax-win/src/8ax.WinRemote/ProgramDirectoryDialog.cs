using System.Collections.ObjectModel;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using EightAxis.WinRemote.Protocol;
using Microsoft.Win32;

namespace EightAxis.WinRemote;

public sealed class ProgramDirectoryDialog : Window
{
    private readonly Func<Task<ProgramListResult>> _refreshAsync;
    private readonly Func<string, Task<ProgramFileInfo>> _statAsync;
    private readonly Func<string, Task<ProgramFileContentResult>> _readAsync;
    private readonly Func<string, Task<ProgramDeleteResult>> _deleteAsync;
    private readonly Func<string, Stream, long, string, bool, Task<ProgramUploadResult>> _uploadAsync;
    private readonly ObservableCollection<ProgramFileDisplay> _files = new();
    private readonly ListView _list = new();
    private readonly TextBox _editor = new();
    private readonly TextBlock _status = new();
    private string? _editingFileName;

    public ProgramDirectoryDialog(
        Func<Task<ProgramListResult>> refreshAsync,
        Func<string, Task<ProgramFileInfo>> statAsync,
        Func<string, Task<ProgramFileContentResult>> readAsync,
        Func<string, Task<ProgramDeleteResult>> deleteAsync,
        Func<string, Stream, long, string, bool, Task<ProgramUploadResult>> uploadAsync)
    {
        _refreshAsync = refreshAsync;
        _statAsync = statAsync;
        _readAsync = readAsync;
        _deleteAsync = deleteAsync;
        _uploadAsync = uploadAsync;

        Title = "\u7cfb\u7edfG\u4ee3\u7801";
        Width = 980;
        Height = 720;
        MinWidth = 820;
        MinHeight = 620;
        Background = new SolidColorBrush(Color.FromRgb(17, 19, 24));
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        UseLayoutRounding = true;

        Content = BuildContent();
        Loaded += async (_, _) => await RefreshAsync();
    }

    private ProgramFileDisplay? SelectedFile => _list.SelectedItem as ProgramFileDisplay;

    private Grid BuildContent()
    {
        Grid root = new() { Margin = new Thickness(18) };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(210) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        TextBlock title = new()
        {
            Text = "\u677f\u7aefG\u4ee3\u7801\u76ee\u5f55",
            Foreground = new SolidColorBrush(Color.FromRgb(242, 245, 248)),
            FontSize = 24,
            FontWeight = FontWeights.SemiBold,
            Margin = new Thickness(0, 0, 0, 12),
        };
        root.Children.Add(title);

        _list.ItemsSource = _files;
        _list.Background = new SolidColorBrush(Color.FromRgb(5, 7, 10));
        _list.Foreground = new SolidColorBrush(Color.FromRgb(226, 238, 246));
        _list.BorderBrush = new SolidColorBrush(Color.FromRgb(70, 119, 146));
        _list.BorderThickness = new Thickness(1);
        _list.FontSize = 16;
        _list.MouseDoubleClick += async (_, _) => await OpenSelectedAsync();
        GridView view = new();
        view.Columns.Add(new GridViewColumn { Header = "\u540d\u79f0", DisplayMemberBinding = new System.Windows.Data.Binding(nameof(ProgramFileDisplay.FileName)), Width = 260 });
        view.Columns.Add(new GridViewColumn { Header = "\u5927\u5c0f", DisplayMemberBinding = new System.Windows.Data.Binding(nameof(ProgramFileDisplay.SizeText)), Width = 100 });
        view.Columns.Add(new GridViewColumn { Header = "\u4fee\u6539\u65f6\u95f4", DisplayMemberBinding = new System.Windows.Data.Binding(nameof(ProgramFileDisplay.ModifiedText)), Width = 170 });
        view.Columns.Add(new GridViewColumn { Header = "SHA256", DisplayMemberBinding = new System.Windows.Data.Binding(nameof(ProgramFileDisplay.ShortSha)), Width = 160 });
        view.Columns.Add(new GridViewColumn { Header = "\u8def\u5f84", DisplayMemberBinding = new System.Windows.Data.Binding(nameof(ProgramFileDisplay.DestinationPath)), Width = 260 });
        _list.View = view;
        Grid.SetRow(_list, 1);
        root.Children.Add(_list);

        _editor.AcceptsReturn = true;
        _editor.AcceptsTab = true;
        _editor.TextWrapping = TextWrapping.NoWrap;
        _editor.VerticalScrollBarVisibility = ScrollBarVisibility.Auto;
        _editor.HorizontalScrollBarVisibility = ScrollBarVisibility.Auto;
        _editor.FontFamily = new FontFamily("Consolas");
        _editor.FontSize = 15;
        _editor.Background = new SolidColorBrush(Color.FromRgb(5, 7, 10));
        _editor.Foreground = new SolidColorBrush(Color.FromRgb(226, 238, 246));
        _editor.BorderBrush = new SolidColorBrush(Color.FromRgb(70, 119, 146));
        _editor.BorderThickness = new Thickness(1);
        _editor.Margin = new Thickness(0, 12, 0, 0);
        Grid.SetRow(_editor, 2);
        root.Children.Add(_editor);

        DockPanel footer = new() { Margin = new Thickness(0, 12, 0, 0), LastChildFill = false };
        _status.Foreground = new SolidColorBrush(Color.FromRgb(183, 193, 204));
        _status.FontSize = 14;
        _status.TextTrimming = TextTrimming.CharacterEllipsis;
        DockPanel.SetDock(_status, Dock.Left);
        footer.Children.Add(_status);

        StackPanel buttons = new() { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right };
        buttons.Children.Add(MakeButton("\u5237\u65b0", async () => await RefreshAsync()));
        buttons.Children.Add(MakeButton("\u4e0a\u4f20", async () => await UploadLocalAsync()));
        buttons.Children.Add(MakeButton("\u6253\u5f00\u4fee\u6539", async () => await OpenSelectedAsync()));
        buttons.Children.Add(MakeButton("\u4fdd\u5b58", async () => await SaveEditAsync()));
        buttons.Children.Add(MakeButton("\u5220\u9664", async () => await DeleteSelectedAsync()));
        buttons.Children.Add(MakeButton("\u5173\u95ed", () => { Close(); return Task.CompletedTask; }));
        DockPanel.SetDock(buttons, Dock.Right);
        footer.Children.Add(buttons);
        Grid.SetRow(footer, 3);
        root.Children.Add(footer);

        return root;
    }

    private Button MakeButton(string text, Func<Task> handler)
    {
        Button button = new()
        {
            Content = text,
            MinWidth = 82,
            Height = 38,
            Margin = new Thickness(8, 0, 0, 0),
            Padding = new Thickness(12, 4, 12, 4),
            Background = new SolidColorBrush(Color.FromRgb(16, 48, 77)),
            Foreground = new SolidColorBrush(Color.FromRgb(238, 245, 248)),
            BorderBrush = new SolidColorBrush(Color.FromRgb(76, 119, 146)),
            BorderThickness = new Thickness(1),
            FontSize = 15,
        };
        button.Click += async (_, _) =>
        {
            button.IsEnabled = false;
            try
            {
                await handler();
            }
            catch (OperationCanceledException ex)
            {
                _status.Text = Compact(ex.Message);
            }
            catch (Exception ex)
            {
                _status.Text = Compact(ex.Message);
                MessageBox.Show(this, Compact(ex.Message), "\u7cfb\u7edfG\u4ee3\u7801", MessageBoxButton.OK, MessageBoxImage.Warning);
            }
            finally
            {
                button.IsEnabled = true;
            }
        };
        return button;
    }

    private async Task RefreshAsync()
    {
        string? selected = SelectedFile?.FileName ?? _editingFileName;
        ProgramListResult result = await _refreshAsync();
        _files.Clear();
        foreach (ProgramFileInfo file in result.Files.OrderBy(item => item.FileName, StringComparer.OrdinalIgnoreCase))
        {
            _files.Add(new ProgramFileDisplay(file));
        }
        if (!String.IsNullOrWhiteSpace(selected))
        {
            _list.SelectedItem = _files.FirstOrDefault(item => String.Equals(item.FileName, selected, StringComparison.OrdinalIgnoreCase));
        }
        _status.Text = $"{result.ProgramDir}  \u5171 {result.Count} \u4e2aG\u4ee3\u7801\u6587\u4ef6";
    }

    private async Task UploadLocalAsync()
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
            return;
        }

        string localPath = dialog.FileName;
        string fileName = Path.GetFileName(localPath);
        bool overwrite = await ConfirmOverwriteIfNeededAsync(fileName);
        await using FileStream stream = new(localPath, FileMode.Open, FileAccess.Read, FileShare.Read);
        string sha256 = await ComputeSha256HexAsync(stream);
        stream.Position = 0;
        ProgramUploadResult upload = await _uploadAsync(fileName, stream, stream.Length, sha256, overwrite);
        _editingFileName = upload.FileName;
        _status.Text = $"\u5df2\u4e0a\u4f20 {upload.FileName}  {FormatBytes(upload.SizeBytes)}";
        await RefreshAsync();
    }

    private async Task OpenSelectedAsync()
    {
        ProgramFileDisplay selected = SelectedFile ?? throw new InvalidOperationException("\u8bf7\u5148\u9009\u62e9\u4e00\u4e2aG\u4ee3\u7801\u6587\u4ef6\u3002");
        if (!selected.Editable)
        {
            throw new InvalidOperationException("\u6587\u4ef6\u8fc7\u5927\uff0c\u4e0d\u80fd\u5728\u7a97\u53e3\u5185\u6253\u5f00\u4fee\u6539\u3002");
        }
        ProgramFileContentResult content = await _readAsync(selected.FileName);
        _editingFileName = content.FileName;
        _editor.Text = content.Text;
        _status.Text = $"\u6b63\u5728\u4fee\u6539 {content.FileName}  {FormatBytes(content.SizeBytes)}";
    }

    private async Task SaveEditAsync()
    {
        if (String.IsNullOrWhiteSpace(_editingFileName))
        {
            throw new InvalidOperationException("\u5f53\u524d\u6ca1\u6709\u6253\u5f00\u4fee\u6539\u7684G\u4ee3\u7801\u6587\u4ef6\u3002");
        }
        MessageBoxResult answer = MessageBox.Show(
            this,
            $"\u4fdd\u5b58\u4f1a\u8986\u76d6\u677f\u7aef\u6587\u4ef6\uff1a{_editingFileName}\n\u662f\u5426\u7ee7\u7eed\uff1f",
            "\u4fdd\u5b58G\u4ee3\u7801",
            MessageBoxButton.YesNo,
            MessageBoxImage.Question);
        if (answer != MessageBoxResult.Yes)
        {
            return;
        }
        string text = _editor.Text;
        if (!text.EndsWith("\n", StringComparison.Ordinal))
        {
            text += Environment.NewLine;
        }
        byte[] payload = Encoding.UTF8.GetBytes(text);
        string sha256 = Convert.ToHexString(SHA256.HashData(payload)).ToLowerInvariant();
        using MemoryStream stream = new(payload);
        ProgramUploadResult upload = await _uploadAsync(_editingFileName, stream, payload.Length, sha256, true);
        _status.Text = $"\u5df2\u4fdd\u5b58 {upload.FileName}  {FormatBytes(upload.SizeBytes)}";
        await RefreshAsync();
    }

    private async Task DeleteSelectedAsync()
    {
        ProgramFileDisplay selected = SelectedFile ?? throw new InvalidOperationException("\u8bf7\u5148\u9009\u62e9\u4e00\u4e2aG\u4ee3\u7801\u6587\u4ef6\u3002");
        MessageBoxResult answer = MessageBox.Show(
            this,
            $"\u5220\u9664\u677f\u7aef\u6587\u4ef6\uff1a{selected.FileName}\n\u662f\u5426\u7ee7\u7eed\uff1f",
            "\u5220\u9664G\u4ee3\u7801",
            MessageBoxButton.YesNo,
            MessageBoxImage.Warning);
        if (answer != MessageBoxResult.Yes)
        {
            return;
        }
        ProgramDeleteResult deleted = await _deleteAsync(selected.FileName);
        if (String.Equals(_editingFileName, selected.FileName, StringComparison.OrdinalIgnoreCase))
        {
            _editingFileName = null;
            _editor.Clear();
        }
        _status.Text = deleted.Deleted ? $"\u5df2\u5220\u9664 {selected.FileName}" : $"\u672a\u5220\u9664 {selected.FileName}";
        await RefreshAsync();
    }

    private async Task<bool> ConfirmOverwriteIfNeededAsync(string fileName)
    {
        ProgramFileInfo info = await _statAsync(fileName);
        if (!info.Exists)
        {
            return false;
        }
        MessageBoxResult answer = MessageBox.Show(
            this,
            $"\u677f\u7aef\u5df2\u7ecf\u6709\u540c\u540d\u6587\u4ef6\uff1a{fileName}\n\u662f\u5426\u8986\u76d6\uff1f",
            "\u8986\u76d6\u786e\u8ba4",
            MessageBoxButton.YesNo,
            MessageBoxImage.Warning);
        if (answer != MessageBoxResult.Yes)
        {
            throw new OperationCanceledException("\u7528\u6237\u53d6\u6d88\u8986\u76d6\u540c\u540dG\u4ee3\u7801\u6587\u4ef6\u3002");
        }
        return true;
    }

    private static async Task<string> ComputeSha256HexAsync(Stream stream)
    {
        byte[] hash = await SHA256.HashDataAsync(stream);
        return Convert.ToHexString(hash).ToLowerInvariant();
    }

    private static string FormatBytes(long bytes)
    {
        string[] units = ["B", "KB", "MB", "GB"];
        double value = Math.Max(0L, bytes);
        int unit = 0;
        while (value >= 1024.0 && unit < units.Length - 1)
        {
            value /= 1024.0;
            unit++;
        }
        return $"{value:0.#}{units[unit]}";
    }

    private static string FormatTime(long? unixMs)
    {
        if (unixMs is null or <= 0)
        {
            return "--";
        }
        return DateTimeOffset.FromUnixTimeMilliseconds(unixMs.Value).ToLocalTime().ToString("yyyy-MM-dd HH:mm:ss");
    }

    private static string ShortSha(string? sha) =>
        String.IsNullOrWhiteSpace(sha) ? "--" : (sha.Length <= 12 ? sha : sha[..12]);

    private static string Compact(string text)
    {
        text = text.Replace("\r", " ").Replace("\n", " ").Trim();
        return text.Length <= 180 ? text : text[..180] + "...";
    }

    private sealed class ProgramFileDisplay
    {
        public ProgramFileDisplay(ProgramFileInfo info)
        {
            Info = info;
            FileName = info.FileName;
            DestinationPath = info.DestinationPath;
            SizeText = info.SizeBytes.HasValue ? FormatBytes(info.SizeBytes.Value) : "--";
            ModifiedText = FormatTime(info.ModifiedAtUnixMs);
            ShortSha = ProgramDirectoryDialog.ShortSha(info.Sha256);
            Editable = info.Editable;
        }

        public ProgramFileInfo Info { get; }
        public string FileName { get; }
        public string DestinationPath { get; }
        public string SizeText { get; }
        public string ModifiedText { get; }
        public string ShortSha { get; }
        public bool Editable { get; }
    }
}
