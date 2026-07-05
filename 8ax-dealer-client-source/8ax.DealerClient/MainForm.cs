using System.Diagnostics;
using System.Security.Cryptography;

namespace EightAxis.DealerClient;

internal sealed class MainForm : Form
{
    private readonly ApiClient _api = new();
    private readonly LocalSettings _settings = LocalSettings.Load();
    private readonly string _machineDigest = MachineFingerprint.CreateDigest();

    private TextBox _username = null!;
    private TextBox _password = null!;
    private Button _loginButton = null!;
    private Button _registerButton = null!;
    private Button _changePasswordButton = null!;
    private Button _updateButton = null!;
    private Button _healthButton = null!;
    private Button _dailyCodeButton = null!;
    private Button _copyInviteButton = null!;
    private TextBox _dailyCode = null!;
    private TextBox _inviteUrl = null!;
    private TextBox _status = null!;
    private Label _account = null!;
    private Label _version = null!;
    private ListView _requestsList = null!;

    private LoginResponse? _session;

    public MainForm()
    {
        Text = "8ax 经销商客户端";
        AutoScaleMode = AutoScaleMode.Dpi;
        Width = 1060;
        Height = 720;
        MinimumSize = new Size(980, 660);
        StartPosition = FormStartPosition.CenterScreen;
        Font = new Font("Microsoft YaHei UI", 9F, FontStyle.Regular, GraphicsUnit.Point);

        BuildUi();
        LoadSettings();
    }

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        _api.Dispose();
        base.OnFormClosed(e);
    }

    private void BuildUi()
    {
        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 5,
            Padding = new Padding(14),
        };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 108));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 132));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 142));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 112));
        Controls.Add(root);

        root.Controls.Add(BuildHeader(), 0, 0);
        root.Controls.Add(BuildLoginPanel(), 0, 1);
        root.Controls.Add(BuildDealerPanel(), 0, 2);
        root.Controls.Add(BuildRequestsPanel(), 0, 3);
        root.Controls.Add(BuildStatusPanel(), 0, 4);
    }

    private Control BuildHeader()
    {
        var panel = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, RowCount = 1 };
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 150));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 260));
        panel.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

        var title = new Label
        {
            Text = "8ax 经销商客户端",
            Dock = DockStyle.Fill,
            Font = new Font(Font.FontFamily, 18F, FontStyle.Bold),
            TextAlign = ContentAlignment.MiddleLeft,
        };
        _version = new Label
        {
            Text = $"版本 {ClientInfo.Version}\r\n{ClientInfo.Build}",
            AutoSize = false,
            Dock = DockStyle.Fill,
            TextAlign = ContentAlignment.MiddleLeft,
        };

        _healthButton = new Button { Text = "连接检查", Width = 108, Height = 30 };
        _healthButton.Click += async (_, _) => await RunGuarded(CheckHealthAsync);
        _updateButton = new Button { Text = "检查升级", Width = 108, Height = 30 };
        _updateButton.Click += async (_, _) => await RunGuarded(CheckUpdateAsync);
        _registerButton = new Button { Text = "注册", Width = 108, Height = 30 };
        _registerButton.Click += async (_, _) => await RegisterDealerAsync();

        var right = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 2,
            Padding = new Padding(0, 8, 0, 6),
        };
        right.RowStyles.Add(new RowStyle(SizeType.Absolute, 36));
        right.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

        var actions = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false,
        };
        actions.Controls.Add(_registerButton);
        actions.Controls.Add(_healthButton);
        actions.Controls.Add(_updateButton);
        right.Controls.Add(actions, 0, 0);
        right.Controls.Add(_version, 0, 1);

        panel.Controls.Add(new Label { Text = "8ax", Dock = DockStyle.Fill, Font = new Font(Font.FontFamily, 16F, FontStyle.Bold), TextAlign = ContentAlignment.MiddleLeft, AutoEllipsis = true }, 0, 0);
        panel.Controls.Add(title, 1, 0);
        panel.Controls.Add(right, 2, 0);

        return panel;
    }

    private Control BuildLoginPanel()
    {
        var group = new GroupBox { Text = "登录", Dock = DockStyle.Fill };
        var grid = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 7, RowCount = 2, Padding = new Padding(10) };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 72));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 40));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 72));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 30));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 16));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 116));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 116));
        grid.RowStyles.Add(new RowStyle(SizeType.Percent, 50));
        grid.RowStyles.Add(new RowStyle(SizeType.Percent, 50));
        group.Controls.Add(grid);

        _username = new TextBox { Dock = DockStyle.Fill };
        _password = new TextBox { Dock = DockStyle.Fill, UseSystemPasswordChar = true };
        _loginButton = new Button { Text = "登录", Dock = DockStyle.Fill };
        _loginButton.Click += async (_, _) => await RunGuarded(LoginAsync);
        _changePasswordButton = new Button { Text = "修改密码", Dock = DockStyle.Fill, Enabled = false };
        _changePasswordButton.Click += async (_, _) => await ChangePasswordAsync();

        _account = new Label { Text = "未登录", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft };

        grid.Controls.Add(_loginButton, 5, 0);
        grid.Controls.Add(_changePasswordButton, 6, 0);
        grid.Controls.Add(new Label { Text = "账号", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, 0);
        grid.Controls.Add(_username, 1, 0);
        grid.Controls.Add(new Label { Text = "密码", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 2, 0);
        grid.Controls.Add(_password, 3, 0);
        grid.Controls.Add(new Label { Text = "状态", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, 1);
        grid.Controls.Add(_account, 1, 1);
        grid.SetColumnSpan(_account, 6);

        return group;
    }

    private Control BuildDealerPanel()
    {
        var group = new GroupBox { Text = "经销商信息", Dock = DockStyle.Fill };
        var grid = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 4, RowCount = 2, Padding = new Padding(10) };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 104));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 45));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 132));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 55));
        grid.RowStyles.Add(new RowStyle(SizeType.Percent, 50));
        grid.RowStyles.Add(new RowStyle(SizeType.Percent, 50));
        group.Controls.Add(grid);

        _dailyCode = new TextBox { Dock = DockStyle.Fill, ReadOnly = true, Font = new Font(Font.FontFamily, 12F, FontStyle.Bold), Multiline = false };
        _dailyCodeButton = new Button { Text = "获取每日码", Dock = DockStyle.Fill, Enabled = false };
        _dailyCodeButton.Click += async (_, _) => await RunGuarded(GetDailyCodeAsync);
        _inviteUrl = new TextBox { Dock = DockStyle.Fill, ReadOnly = true };
        _copyInviteButton = new Button { Text = "复制员工注册链接", Dock = DockStyle.Fill, Enabled = false };
        _copyInviteButton.Click += (_, _) => CopyInviteUrl();

        grid.Controls.Add(new Label { Text = "每日校验码", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, 0);
        grid.Controls.Add(_dailyCode, 1, 0);
        grid.Controls.Add(_dailyCodeButton, 2, 0);
        grid.Controls.Add(new Label { Text = "仅主账号或授权账号可见", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 3, 0);
        grid.Controls.Add(new Label { Text = "员工注册", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, 1);
        grid.Controls.Add(_inviteUrl, 1, 1);
        grid.SetColumnSpan(_inviteUrl, 2);
        grid.Controls.Add(_copyInviteButton, 3, 1);

        return group;
    }

    private Control BuildRequestsPanel()
    {
        var group = new GroupBox { Text = "终端升级需求（经销商/员工同步）", Dock = DockStyle.Fill };
        var grid = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, Padding = new Padding(10) };
        _requestsList = new ListView
        {
            Dock = DockStyle.Fill,
            View = View.Details,
            FullRowSelect = true,
            GridLines = true,
            HideSelection = false,
        };
        _requestsList.Columns.Add("时间", 120);
        _requestsList.Columns.Add("设备", 140);
        _requestsList.Columns.Add("升级意图", 190);
        _requestsList.Columns.Add("状态", 120);
        _requestsList.Columns.Add("说明", 320);
        _requestsList.Items.Add(new ListViewItem(new[] { "-", "-", "等待服务端接口上线", "未连接", "后续接 /api/v1/dealer/upgrade-requests，主账号和员工账号同步显示" }));
        _requestsList.Resize += (_, _) => AdjustRequestColumns();
        grid.Controls.Add(_requestsList);
        group.Controls.Add(grid);
        return group;
    }

    protected override void OnShown(EventArgs e)
    {
        base.OnShown(e);
        AdjustRequestColumns();
    }

    private void AdjustRequestColumns()
    {
        if (_requestsList.Columns.Count == 0 || _requestsList.ClientSize.Width <= 0)
        {
            return;
        }

        var width = Math.Max(760, _requestsList.ClientSize.Width - SystemInformation.VerticalScrollBarWidth - 8);
        _requestsList.Columns[0].Width = Math.Max(100, (int)(width * 0.13));
        _requestsList.Columns[1].Width = Math.Max(120, (int)(width * 0.16));
        _requestsList.Columns[2].Width = Math.Max(170, (int)(width * 0.24));
        _requestsList.Columns[3].Width = Math.Max(100, (int)(width * 0.14));
        _requestsList.Columns[4].Width = Math.Max(220, width - _requestsList.Columns[0].Width - _requestsList.Columns[1].Width - _requestsList.Columns[2].Width - _requestsList.Columns[3].Width);
    }

    private Control BuildStatusPanel()
    {
        var group = new GroupBox { Text = "运行日志", Dock = DockStyle.Fill };
        _status = new TextBox
        {
            Dock = DockStyle.Fill,
            Multiline = true,
            ScrollBars = ScrollBars.Vertical,
            ReadOnly = true,
        };
        group.Controls.Add(_status);
        return group;
    }

    private void LoadSettings()
    {
        _username.Text = _settings.LastUsername;
        ApplyServerUrl();
        Log("客户端启动。");
        Log("机器摘要：" + _machineDigest[..16] + "...");
    }

    private async Task CheckHealthAsync()
    {
        ApplyServerUrl();
        var result = await _api.HealthAsync(CancellationToken.None);
        if (!result.Success)
        {
            Log(result.Error);
            return;
        }

        Log($"服务器正常：{result.Value?.Service} {result.Value?.Version} {result.Value?.Time}");
    }

    private async Task CheckUpdateAsync()
    {
        ApplyServerUrl();
        var request = new UpdateCheckRequest(
            ClientInfo.Version,
            ClientInfo.Build,
            ClientInfo.Channel,
            Environment.OSVersion.VersionString,
            _machineDigest);

        var result = await _api.CheckUpdateAsync(request, CancellationToken.None);
        if (!result.Success)
        {
            Log(result.Error);
            return;
        }

        var update = result.Value!;
        if (update.ForceUpdate)
        {
            Log($"需要强制升级到 {update.LatestVersion}：{update.Message}");
            await DownloadAndLaunchUpdateAsync(update);
            return;
        }

        if (!update.AllowContinue && !string.IsNullOrWhiteSpace(update.DownloadUrl))
        {
            Log($"发现新版本 {update.LatestVersion}：{update.Message}");
            await DownloadAndLaunchUpdateAsync(update);
            return;
        }

        Log(update.AllowContinue ? $"客户端版本可继续使用，最新版本：{update.LatestVersion}" : $"当前版本不可继续使用：{update.Message}");
    }

    private async Task DownloadAndLaunchUpdateAsync(UpdateCheckResponse update)
    {
        if (string.IsNullOrWhiteSpace(update.DownloadUrl))
        {
            Log("服务端没有返回升级包下载地址。");
            return;
        }

        var answer = MessageBox.Show(
            this,
            $"发现新版 {update.LatestVersion}，是否现在下载并启动新版？\n\n当前程序会在启动新版后退出。",
            "软件升级",
            MessageBoxButtons.YesNo,
            MessageBoxIcon.Information);
        if (answer != DialogResult.Yes)
        {
            Log("已取消本次升级。");
            return;
        }

        var currentExe = Process.GetCurrentProcess().MainModule?.FileName;
        if (string.IsNullOrWhiteSpace(currentExe))
        {
            Log("无法识别当前程序路径，已取消升级。");
            return;
        }

        var version = string.IsNullOrWhiteSpace(update.LatestVersion) ? "latest" : update.LatestVersion;
        var dir = Path.Combine(Path.GetDirectoryName(currentExe)!, "updates", version);
        Directory.CreateDirectory(dir);

        var fileName = Path.GetFileName(new Uri(update.DownloadUrl).LocalPath);
        if (string.IsNullOrWhiteSpace(fileName))
        {
            fileName = "8ax.DealerClient.update.exe";
        }

        var target = Path.Combine(dir, fileName);
        Log("正在下载升级包：" + update.DownloadUrl);

        using var http = new HttpClient();
        using var response = await http.GetAsync(update.DownloadUrl);
        response.EnsureSuccessStatusCode();
        await using (var fs = File.Create(target))
        {
            await response.Content.CopyToAsync(fs);
        }

        if (!string.IsNullOrWhiteSpace(update.Sha256))
        {
            var actual = Convert.ToHexString(await SHA256.HashDataAsync(File.OpenRead(target))).ToUpperInvariant();
            if (!string.Equals(actual, update.Sha256.Trim(), StringComparison.OrdinalIgnoreCase))
            {
                Log("升级包 SHA256 校验失败，已拒绝启动。");
                MessageBox.Show(this, "升级包校验失败，已拒绝启动。", "软件升级", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }
        }

        Log("升级包校验通过，程序退出后会覆盖当前 exe 并从原路径重新启动。");
        StartReplacementScript(target, currentExe);
        Application.Exit();
    }

    private static void StartReplacementScript(string sourceExe, string targetExe)
    {
        var scriptPath = Path.Combine(Path.GetDirectoryName(sourceExe)!, "apply-update.cmd");
        var logPath = Path.Combine(Path.GetDirectoryName(sourceExe)!, "apply-update.log");
        var currentPid = Environment.ProcessId;
        var script = $$"""
            @echo off
            setlocal
            set "SRC={{sourceExe}}"
            set "DST={{targetExe}}"
            set "LOG={{logPath}}"
            echo [%date% %time%] waiting pid {{currentPid}} > "%LOG%"
            :wait
            tasklist /FI "PID eq {{currentPid}}" | find "{{currentPid}}" >nul
            if not errorlevel 1 (
              timeout /t 1 /nobreak >nul
              goto wait
            )
            echo [%date% %time%] copying "%SRC%" to "%DST%" >> "%LOG%"
            copy /Y "%SRC%" "%DST%" >> "%LOG%" 2>&1
            if errorlevel 1 (
              echo [%date% %time%] copy failed >> "%LOG%"
              exit /b 1
            )
            echo [%date% %time%] starting "%DST%" >> "%LOG%"
            start "" "%DST%"
            del "%~f0"
            """;
        File.WriteAllText(scriptPath, script);
        Process.Start(new ProcessStartInfo
        {
            FileName = scriptPath,
            UseShellExecute = true,
            WindowStyle = ProcessWindowStyle.Hidden,
        });
    }

    private async Task LoginAsync()
    {
        ApplyServerUrl();
        if (string.IsNullOrWhiteSpace(_username.Text) || string.IsNullOrWhiteSpace(_password.Text))
        {
            Log("请输入账号和密码。");
            return;
        }

        _settings.LastUsername = _username.Text.Trim();
        _settings.Save();

        var request = new LoginRequest(
            _username.Text.Trim(),
            _password.Text,
            _machineDigest,
            ClientInfo.Version,
            ClientInfo.Build,
            ClientInfo.Channel,
            Environment.OSVersion.VersionString);

        var result = await _api.LoginAsync(request, CancellationToken.None);
        if (!result.Success)
        {
            Log(result.Error);
            return;
        }

        _session = result.Value;
        _api.SessionToken = _session?.SessionToken;
        _account.Text = $"{_session?.DealerName ?? "-"} / {_session?.Username ?? _username.Text} / {_session?.AccountType ?? _session?.Role ?? "-"}";
        _changePasswordButton.Enabled = true;
        _dailyCodeButton.Enabled = _session?.CanShowDailyCode == true;
        _copyInviteButton.Enabled = !string.IsNullOrWhiteSpace(_session?.InviteRegisterUrl);
        _inviteUrl.Text = _session?.InviteRegisterUrl ?? "";
        Log("登录成功。");
    }

    private async Task RegisterDealerAsync()
    {
        ApplyServerUrl();
        using var form = new RegisterDealerForm(_machineDigest);
        if (form.ShowDialog(this) != DialogResult.OK || form.Request is null)
        {
            return;
        }

        await RunGuarded(async () =>
        {
            var result = await _api.RegisterDealerAsync(form.Request, CancellationToken.None);
            if (!result.Success)
            {
                Log(result.Error);
                return;
            }

            var response = result.Value;
            Log($"注册资料已提交，审核状态：{response?.ReviewStatus ?? "pending_review"}，经销商ID：{response?.DealerId ?? "-"}");
        });
    }

    private async Task ChangePasswordAsync()
    {
        if (_session is null)
        {
            Log("请先登录。");
            return;
        }

        using var form = new ChangePasswordForm();
        if (form.ShowDialog(this) != DialogResult.OK)
        {
            return;
        }

        await RunGuarded(async () =>
        {
            var request = new ChangePasswordRequest(
                form.OldPassword,
                form.NewPassword,
                _machineDigest,
                ClientInfo.Version,
                ClientInfo.Build);
            var result = await _api.ChangePasswordAsync(request, CancellationToken.None);
            if (!result.Success)
            {
                Log(result.Error);
                return;
            }

            Log(result.Value?.Message ?? "密码已修改。");
            _password.Clear();
        });
    }

    private async Task GetDailyCodeAsync()
    {
        if (_session is null)
        {
            Log("请先登录。");
            return;
        }

        var result = await _api.GetDailyCodeAsync(new DailyCodeRequest(null, null, "manual"), CancellationToken.None);
        if (!result.Success)
        {
            Log(result.Error);
            return;
        }

        _dailyCode.Text = result.Value?.DealerDailyCode ?? "";
        Log($"每日码有效期：{result.Value?.ExpiresAt?.LocalDateTime.ToString() ?? "-"}，剩余次数：{result.Value?.RemainingUses?.ToString() ?? "-"}");
    }

    private void CopyInviteUrl()
    {
        if (string.IsNullOrWhiteSpace(_inviteUrl.Text))
        {
            Log("当前没有可复制的员工注册链接。");
            return;
        }

        Clipboard.SetText(_inviteUrl.Text);
        Log("员工注册链接已复制。");
    }

    private void ApplyServerUrl()
    {
        _api.BaseUrl = LocalSettings.PrimaryServerUrl;
        _settings.ServerUrl = _api.BaseUrl;
        _settings.Save();
    }

    private async Task RunGuarded(Func<Task> action)
    {
        SetBusy(true);
        try
        {
            await action();
        }
        catch (Exception ex)
        {
            Log("操作失败：" + ex.Message);
            Debug.WriteLine(ex);
        }
        finally
        {
            SetBusy(false);
        }
    }

    private void SetBusy(bool busy)
    {
        _loginButton.Enabled = !busy;
        _registerButton.Enabled = !busy;
        _healthButton.Enabled = !busy;
        _updateButton.Enabled = !busy;
        _changePasswordButton.Enabled = !busy && _session is not null;
        _dailyCodeButton.Enabled = !busy && _session?.CanShowDailyCode == true;
        _copyInviteButton.Enabled = !busy && !string.IsNullOrWhiteSpace(_inviteUrl.Text);
        Cursor = busy ? Cursors.WaitCursor : Cursors.Default;
    }

    private void Log(string message)
    {
        _status.AppendText($"[{DateTime.Now:HH:mm:ss}] {message}{Environment.NewLine}");
    }
}
