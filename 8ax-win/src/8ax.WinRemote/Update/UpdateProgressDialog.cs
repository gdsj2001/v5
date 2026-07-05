using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace EightAxis.WinRemote.Update;

public sealed class UpdateProgressDialog : Window
{
    private const string UpgradeRunningHint = "\u8bf7\u4e0d\u8981\u5173\u95ed\u7a0b\u5e8f\u3002\u4e0b\u8f7d\u5b8c\u6210\u540e\u4f1a\u81ea\u52a8\u91cd\u542f\u5e76\u5b89\u88c5\u3002";
    private const string AlreadyCurrentHint = "\u5f53\u524d\u5df2\u662f\u6700\u65b0\u7248\u672c\uff0c\u65e0\u9700\u4e0b\u8f7d\u3001\u5b89\u88c5\u6216\u91cd\u542f\u3002";
    private const string FailedHint = "\u5347\u7ea7\u6ca1\u6709\u5b8c\u6210\uff0c\u8bf7\u68c0\u67e5\u7f51\u7edc\u6216\u7a0d\u540e\u91cd\u8bd5\u3002";

    private readonly TextBlock _stageText = new();
    private readonly TextBlock _messageText = new();
    private readonly TextBlock _hintText = new();
    private readonly ProgressBar _progressBar = new();
    private readonly Button _closeButton = new();

    public UpdateProgressDialog()
    {
        Title = "8ax WinRemote 升级";
        Width = 460;
        MinWidth = 460;
        SizeToContent = SizeToContent.Height;
        ResizeMode = ResizeMode.NoResize;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        ShowInTaskbar = true;
        Topmost = true;
        Background = BrushFrom("#111318");
        Foreground = BrushFrom("#F2F5F8");
        UseLayoutRounding = true;
        SnapsToDevicePixels = true;

        StackPanel root = new() { Margin = new Thickness(24, 22, 24, 20) };

        _stageText.FontSize = 20;
        _stageText.FontWeight = FontWeights.SemiBold;
        _stageText.Margin = new Thickness(0, 0, 0, 12);
        _stageText.Text = "准备升级";
        root.Children.Add(_stageText);

        _messageText.FontSize = 14;
        _messageText.Foreground = BrushFrom("#B9C5D1");
        _messageText.TextWrapping = TextWrapping.Wrap;
        _messageText.MinHeight = 42;
        _messageText.Text = "正在准备升级任务";
        root.Children.Add(_messageText);

        _progressBar.Height = 18;
        _progressBar.Margin = new Thickness(0, 16, 0, 14);
        _progressBar.Minimum = 0;
        _progressBar.Maximum = 100;
        _progressBar.IsIndeterminate = true;
        _progressBar.Foreground = BrushFrom("#24C7F0");
        root.Children.Add(_progressBar);

        _hintText.Text = UpgradeRunningHint;
        _hintText.Foreground = BrushFrom("#8792A0");
        _hintText.FontSize = 12;
        _hintText.TextWrapping = TextWrapping.Wrap;
        _hintText.Margin = new Thickness(0, 0, 0, 18);
        root.Children.Add(_hintText);

        _closeButton.Content = "关闭";
        _closeButton.Width = 96;
        _closeButton.Height = 28;
        _closeButton.HorizontalAlignment = HorizontalAlignment.Right;
        _closeButton.IsEnabled = false;
        _closeButton.Click += (_, _) => Close();
        root.Children.Add(_closeButton);

        Content = root;
    }

    public void Report(UpdateProgress progress)
    {
        _stageText.Text = progress.Stage;
        _messageText.Text = progress.Message;
        _hintText.Text = UpgradeRunningHint;
        if (progress.Percent.HasValue)
        {
            _progressBar.IsIndeterminate = false;
            _progressBar.Value = progress.Percent.Value;
        }
        else
        {
            _progressBar.IsIndeterminate = true;
        }
    }

    public void MarkFailed(string message)
    {
        Report(UpdateProgress.Indeterminate("升级失败", message));
        _hintText.Text = FailedHint;
        _progressBar.IsIndeterminate = false;
        _progressBar.Value = 0;
        _closeButton.IsEnabled = true;
    }

    public void MarkDone(string message)
    {
        Report(UpdateProgress.PercentValue("已是最新", message, 100.0));
        _hintText.Text = AlreadyCurrentHint;
        _closeButton.IsEnabled = true;
    }

    private static Brush BrushFrom(string value) =>
        (Brush)new BrushConverter().ConvertFromString(value)!;
}
