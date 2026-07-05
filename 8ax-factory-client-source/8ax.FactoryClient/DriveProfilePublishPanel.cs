using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace EightAxis.FactoryClient;

internal sealed class DriveProfilePublishPanel : UserControl
{
    private static readonly Regex Sha256Hex = new(@"^[0-9a-fA-F]{64}$", RegexOptions.Compiled);
    private static readonly Regex VpsId = new(@"^[0-9]{6}$", RegexOptions.Compiled);

    private readonly Func<ApiClient> _apiProvider;
    private readonly Action<string> _log;
    private readonly ComboBox _scope = new() { Dock = DockStyle.Fill, DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly TextBox _privateId = new() { Dock = DockStyle.Fill, PlaceholderText = "private 发布必填：6位 VPS分发ID" };
    private readonly TextBox _privateHash = new() { Dock = DockStyle.Fill, PlaceholderText = "private 发布必填：64位 PL DNA hash" };
    private readonly TextBox _profilePath = new() { Dock = DockStyle.Fill, ReadOnly = true };
    private readonly TextBox _reason = new() { Dock = DockStyle.Fill, Multiline = true, ScrollBars = ScrollBars.Vertical };
    private readonly TextBox _output = new() { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical };
    private readonly Button _publish = new() { Text = "发布驱动", Dock = DockStyle.Fill };
    private readonly Button _cancel = new() { Text = "取消发布", Dock = DockStyle.Fill, Enabled = false };
    private CancellationTokenSource? _publishCts;

    public DriveProfilePublishPanel(Func<ApiClient> apiProvider, Action<string> log)
    {
        _apiProvider = apiProvider;
        _log = log;
        Dock = DockStyle.Fill;
        BuildUi();
    }

    private bool IsPrivateScope => string.Equals(_scope.SelectedItem?.ToString(), "private", StringComparison.OrdinalIgnoreCase);

    private void BuildUi()
    {
        _scope.Items.AddRange(new object[] { "public", "private" });
        _scope.SelectedIndex = 0;
        _scope.SelectedIndexChanged += (_, _) => SetPrivateInputsEnabled();
        SetPrivateInputsEnabled();

        var root = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 2, ColumnCount = 1, Padding = new Padding(10) };
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 56));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 44));
        Controls.Add(root);

        var form = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 4, RowCount = 7 };
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 130));
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 150));
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        root.Controls.Add(form, 0, 0);

        AddRow(form, 0, "发布范围", _scope, "私有ID", _privateId);
        AddRow(form, 1, "DNA private hash", _privateHash, "", new Label());
        AddFileRow(form, 2, "驱动映射表", _profilePath, "选择JSON", ChooseProfileFile);
        form.Controls.Add(new Label { Text = "发布原因", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, 3);
        form.Controls.Add(_reason, 1, 3);
        form.SetColumnSpan(_reason, 3);
        var actions = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3 };
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 120));
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 120));
        actions.Controls.Add(new Label { Text = "private 目录只使用 6 位私有ID，DNA hash 仅用于 VPS 校验。", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, 0);
        actions.Controls.Add(_publish, 1, 0);
        actions.Controls.Add(_cancel, 2, 0);
        form.Controls.Add(actions, 0, 4);
        form.SetColumnSpan(actions, 4);
        _publish.Click += async (_, _) => await PublishAsync();
        _cancel.Click += (_, _) => _publishCts?.Cancel();

        var resultGroup = new GroupBox { Text = "发布结果 / profile 摘要", Dock = DockStyle.Fill };
        resultGroup.Controls.Add(_output);
        root.Controls.Add(resultGroup, 0, 1);
    }

    private void SetPrivateInputsEnabled()
    {
        _privateId.Enabled = IsPrivateScope;
        _privateHash.Enabled = IsPrivateScope;
    }

    private static void AddRow(TableLayoutPanel form, int row, string leftLabel, Control left, string rightLabel, Control right)
    {
        form.RowStyles.Add(new RowStyle(SizeType.Absolute, 36));
        form.Controls.Add(new Label { Text = leftLabel, Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, row);
        form.Controls.Add(left, 1, row);
        form.Controls.Add(new Label { Text = rightLabel, Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 2, row);
        form.Controls.Add(right, 3, row);
    }

    private static void AddFileRow(TableLayoutPanel form, int row, string label, TextBox path, string buttonText, Action choose)
    {
        form.RowStyles.Add(new RowStyle(SizeType.Absolute, 36));
        form.Controls.Add(new Label { Text = label, Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, row);
        form.Controls.Add(path, 1, row);
        form.SetColumnSpan(path, 2);
        var button = new Button { Text = buttonText, Dock = DockStyle.Fill };
        button.Click += (_, _) => choose();
        form.Controls.Add(button, 3, row);
    }

    private void ChooseProfileFile()
    {
        using var dialog = new OpenFileDialog
        {
            Title = "选择 driver_profile_map.json",
            CheckFileExists = true,
            Multiselect = false,
            Filter = "Driver profile (driver_profile_map.json)|driver_profile_map.json|JSON files (*.json)|*.json|All files (*.*)|*.*",
        };
        if (dialog.ShowDialog(this) == DialogResult.OK)
        {
            _profilePath.Text = dialog.FileName;
        }
    }

    private async Task PublishAsync()
    {
        _publish.Enabled = false;
        _cancel.Enabled = true;
        _publishCts = new CancellationTokenSource();
        try
        {
            DriveProfilePublishRequest request = await BuildRequestAsync(_publishCts.Token);
            _output.Text = BuildPreview(request) + "\r\n正在上传 VPS...";
            var result = await _apiProvider().PublishDriveProfileAsync(request, _publishCts.Token);
            if (!result.Success)
            {
                _output.Text = BuildPreview(request) + "\r\n\r\n发布失败：\r\n" + result.Error;
                _log("驱动发布失败：" + result.Error);
                MessageBox.Show(this, result.Error, "驱动发布失败", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            _output.Text = BuildPreview(request) + "\r\n\r\nVPS返回：\r\n" + BuildResponse(result.Value);
            _log(result.Value?.Message ?? "驱动 profile 已上传 VPS。");
            MessageBox.Show(this, result.Value?.Message ?? "驱动 profile 已上传 VPS。", "驱动发布完成", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        catch (OperationCanceledException)
        {
            _output.AppendText("\r\n发布已取消。");
            _log("驱动发布已取消。");
        }
        catch (Exception ex)
        {
            _output.AppendText("\r\n发布失败：" + ex.Message);
            _log("驱动发布失败：" + ex.Message);
            MessageBox.Show(this, ex.Message, "驱动发布失败", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
        finally
        {
            _publishCts.Dispose();
            _publishCts = null;
            _cancel.Enabled = false;
            _publish.Enabled = true;
        }
    }

    private async Task<DriveProfilePublishRequest> BuildRequestAsync(CancellationToken cancellationToken)
    {
        string scope = _scope.SelectedItem?.ToString() ?? "public";
        string privateId = _privateId.Text.Trim();
        string privateHash = _privateHash.Text.Trim().ToLowerInvariant();
        if (scope == "private" && !VpsId.Match(privateId).Success)
        {
            throw new InvalidOperationException("private 发布必须填写 6 位 VPS分发ID。");
        }
        if (scope == "private" && !Sha256Hex.Match(privateHash).Success)
        {
            throw new InvalidOperationException("private 发布必须填写 64 位 PL DNA hash。");
        }
        string profilePath = RequireProfileFile(_profilePath.Text);
        string reason = _reason.Text.Trim();
        if (string.IsNullOrWhiteSpace(reason))
        {
            throw new InvalidOperationException("发布原因必填。");
        }
        FileInfo profileInfo = new(profilePath);
        string profileSha = await ComputeSha256Async(profilePath, cancellationToken);
        return new DriveProfilePublishRequest(
            scope,
            scope == "private" ? privateId : null,
            scope == "private" ? privateHash : null,
            profilePath,
            profileSha,
            profileInfo.Length,
            reason);
    }

    private static string RequireProfileFile(string value)
    {
        string path = value.Trim();
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
        {
            throw new InvalidOperationException("驱动映射表不存在。");
        }
        if (!string.Equals(Path.GetFileName(path), "driver_profile_map.json", StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException("驱动映射表文件名必须是 driver_profile_map.json。");
        }
        return path;
    }

    private static async Task<string> ComputeSha256Async(string path, CancellationToken cancellationToken)
    {
        await using FileStream stream = new(path, FileMode.Open, FileAccess.Read, FileShare.Read, 1024 * 1024, FileOptions.Asynchronous | FileOptions.SequentialScan);
        byte[] hash = await SHA256.HashDataAsync(stream, cancellationToken);
        return Convert.ToHexString(hash).ToLowerInvariant();
    }

    private static string BuildPreview(DriveProfilePublishRequest request)
    {
        string target = request.Scope == "private"
            ? $"private/{request.PrivateId}/driver_profile_map.json"
            : "public/driver_profile_map.json";
        return
            $"scope: {request.Scope}\r\n" +
            $"target: {target}\r\n" +
            $"profile: {Path.GetFileName(request.ProfilePath)}\r\n" +
            $"profile_sha256: {request.ProfileSha256}\r\n" +
            "operator_confirmation_required: true";
    }

    private static string BuildResponse(DriveProfilePublishResponse? response)
    {
        if (response is null)
        {
            return "服务端未返回发布详情。";
        }
        return
            $"success: {response.Success}\r\n" +
            $"message: {response.Message ?? "-"}\r\n" +
            $"source_scope: {response.SourceScope ?? "-"}\r\n" +
            $"target_rel: {response.TargetRel ?? "-"}\r\n" +
            $"vps_distribution_id: {response.VpsDistributionId ?? "-"}\r\n" +
            $"dna_binding: {response.DnaBinding ?? "-"}\r\n" +
            $"profile_sha256: {response.ProfileSha256 ?? "-"}";
    }
}
