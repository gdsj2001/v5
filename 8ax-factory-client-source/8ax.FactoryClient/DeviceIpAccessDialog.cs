using System.Diagnostics;

namespace EightAxis.FactoryClient;

internal sealed class DeviceIpAccessDialog : Form
{
    private readonly DeviceRecord _device;
    private readonly List<DeviceIpAccessRecord> _records;
    private readonly ApiClient _api;
    private readonly Action<string> _log;
    private ListView _summary = null!;
    private ListView _ipStats = null!;
    private ListView _pathStats = null!;
    private readonly DataGridView _details = new();
    private readonly TextBox _deleteNote = new();
    private Button _remoteSsh = null!;

    public string DeleteNonce { get; private set; } = "";
    public string DeleteNote => _deleteNote.Text.Trim();

    public DeviceIpAccessDialog(DeviceRecord device, ApiClient api, Action<string> log)
    {
        _device = device;
        _api = api;
        _log = log;
        _records = (device.IpAccessRecords ?? [])
            .OrderByDescending(item => item.Time)
            .ToList();

        Text = $"IP访问记录 - 设备 {device.DeviceId}";
        AutoScaleMode = AutoScaleMode.Dpi;
        StartPosition = FormStartPosition.CenterParent;
        Width = 1100;
        Height = 720;
        MinimumSize = new Size(920, 600);
        Font = new Font("Microsoft YaHei UI", 9F, FontStyle.Regular, GraphicsUnit.Point);

        BuildUi();
        LoadData();
    }

    private void BuildUi()
    {
        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 4,
            Padding = new Padding(12),
        };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 78));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 44));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 48));
        Controls.Add(root);

        var header = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 4 };
        header.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 88));
        header.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        header.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 88));
        header.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        header.RowStyles.Add(new RowStyle(SizeType.Percent, 50));
        header.RowStyles.Add(new RowStyle(SizeType.Percent, 50));
        AddHeader(header, "设备ID", _device.DeviceId, 0, 0);
        AddHeader(header, "PL DNA", _device.PlDeviceDna ?? "-", 2, 0);
        AddHeader(header, "DNA摘要", _device.PlDnaHash ?? "-", 0, 1);
        AddHeader(header, "状态", _device.ActivationStatus ?? "-", 2, 1);
        root.Controls.Add(header, 0, 0);

        var tabs = new TabControl { Dock = DockStyle.Fill };
        tabs.TabPages.Add(CreateTab("统计", BuildSummaryList()));
        tabs.TabPages.Add(CreateTab("明细", BuildDetailGrid()));
        tabs.TabPages.Add(CreateTab("IP分布", BuildIpStatsList()));
        tabs.TabPages.Add(CreateTab("路径统计", BuildPathStatsList()));

        var actions = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 4 };
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 130));
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 100));
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 170));
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        _deleteNote.Dock = DockStyle.Fill;
        _deleteNote.PlaceholderText = "删除原因，可空";
        var delete = new Button { Text = "删除选中记录", Dock = DockStyle.Fill };
        delete.Click += (_, _) => DeleteSelected();
        var refreshHint = new Label { Text = "删除后会自动刷新设备列表", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleCenter };
        var close = new Button { Text = "关闭", Dock = DockStyle.Fill };
        close.Click += (_, _) => Close();
        actions.Controls.Add(delete, 0, 0);
        actions.Controls.Add(close, 1, 0);
        actions.Controls.Add(refreshHint, 2, 0);
        actions.Controls.Add(_deleteNote, 3, 0);
        root.Controls.Add(actions, 0, 1);
        root.Controls.Add(tabs, 0, 2);

        var remoteActions = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, Padding = new Padding(0, 6, 0, 0) };
        remoteActions.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        remoteActions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 260));
        var remoteHint = new Label
        {
            Text = "访问记录 IP 仅用于出站审计；SSH 按设备 ID 通过 VPS 反向隧道路由",
            Dock = DockStyle.Fill,
            TextAlign = ContentAlignment.MiddleRight,
        };
        _remoteSsh = new Button { Dock = DockStyle.Fill };
        _remoteSsh.Click += async (_, _) => await ConnectRemoteSshAsync();
        remoteActions.Controls.Add(remoteHint, 0, 0);
        remoteActions.Controls.Add(_remoteSsh, 1, 0);
        root.Controls.Add(remoteActions, 0, 3);
    }

    private static TabPage CreateTab(string title, Control content)
    {
        var page = new TabPage(title) { Padding = new Padding(8) };
        page.Controls.Add(content);
        return page;
    }

    private static void AddHeader(TableLayoutPanel parent, string label, string value, int col, int row)
    {
        parent.Controls.Add(new Label { Text = label, Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, col, row);
        parent.Controls.Add(new TextBox { Text = value, Dock = DockStyle.Fill, ReadOnly = true }, col + 1, row);
    }

    private ListView BuildSummaryList()
    {
        _summary = NewList(("项目", 180), ("值", 360));
        return _summary;
    }

    private ListView BuildIpStatsList()
    {
        _ipStats = NewList(("IP", 170), ("总数", 70), ("下载", 70), ("挑战", 70), ("最近访问", 170), ("路径数", 90));
        return _ipStats;
    }

    private DataGridView BuildDetailGrid()
    {
        _details.Dock = DockStyle.Fill;
        _details.AllowUserToAddRows = false;
        _details.AllowUserToDeleteRows = false;
        _details.AllowUserToResizeRows = false;
        _details.AutoGenerateColumns = false;
        _details.BackgroundColor = SystemColors.Window;
        _details.BorderStyle = BorderStyle.FixedSingle;
        _details.ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.AutoSize;
        _details.MultiSelect = false;
        _details.ReadOnly = true;
        _details.RowHeadersVisible = false;
        _details.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
        AddDetailColumn("time", "时间", 140);
        AddDetailColumn("ip", "IP", 150);
        AddDetailColumn("status", "状态", 70);
        AddDetailColumn("path", "请求路径", 520);
        AddDetailColumn("nonce", "记录ID", 210);
        return _details;
    }

    private ListView BuildPathStatsList()
    {
        _pathStats = NewList(("请求路径", 720), ("次数", 80), ("最近访问", 170));
        return _pathStats;
    }

    private static ListView NewList(params (string Header, int Width)[] columns)
    {
        var group = new ListView
        {
            Dock = DockStyle.Fill,
            View = View.Details,
            FullRowSelect = true,
            GridLines = true,
            HideSelection = false,
        };
        foreach (var (header, width) in columns)
        {
            group.Columns.Add(header, width);
        }
        return group;
    }

    private void AddDetailColumn(string name, string header, int width)
    {
        _details.Columns.Add(new DataGridViewTextBoxColumn
        {
            Name = name,
            HeaderText = header,
            Width = width,
            SortMode = DataGridViewColumnSortMode.NotSortable,
        });
    }

    private void LoadData()
    {
        UpdateRemoteSshButton();
        LoadSummary(_summary);
        LoadIpStats(_ipStats);
        LoadDetails();
        LoadPathStats(_pathStats);
    }

    private void LoadSummary(ListView list)
    {
        list.Items.Clear();
        var downloadCount = _records.Count(IsDownload);
        var challengeCount = _records.Count - downloadCount;
        var uniqueIps = _records.Select(item => Clean(item.Ip)).Where(item => item != "-").Distinct(StringComparer.OrdinalIgnoreCase).Count();
        var uniquePaths = _records.Select(item => Clean(item.Path)).Where(item => item != "-").Distinct(StringComparer.OrdinalIgnoreCase).Count();
        var latest = _records.OrderByDescending(item => item.Time).FirstOrDefault();
        var earliest = _records.OrderBy(item => item.Time).FirstOrDefault();
        AddSummary(list, "总记录数", _records.Count.ToString());
        AddSummary(list, "下载完成", downloadCount.ToString());
        AddSummary(list, "仅挑战未下载", challengeCount.ToString());
        AddSummary(list, "独立IP数", uniqueIps.ToString());
        AddSummary(list, "请求路径数", uniquePaths.ToString());
        AddSummary(list, "最近访问", latest is null ? "-" : FormatTime(latest.Time));
        AddSummary(list, "最近IP", latest is null ? "-" : Clean(latest.Ip));
        AddSummary(list, "最早访问", earliest is null ? "-" : FormatTime(earliest.Time));
    }

    private static void AddSummary(ListView list, string name, string value)
    {
        list.Items.Add(new ListViewItem(new[] { name, value }));
    }

    private void LoadIpStats(ListView list)
    {
        list.Items.Clear();
        foreach (var group in _records.GroupBy(item => Clean(item.Ip)).OrderByDescending(group => group.Count()).ThenBy(group => group.Key))
        {
            var latest = group.OrderByDescending(item => item.Time).FirstOrDefault();
            var paths = group.Select(item => Clean(item.Path)).Where(item => item != "-").Distinct(StringComparer.OrdinalIgnoreCase).Count();
            list.Items.Add(new ListViewItem(new[]
            {
                group.Key,
                group.Count().ToString(),
                group.Count(IsDownload).ToString(),
                group.Count(item => !IsDownload(item)).ToString(),
                FormatTime(latest?.Time),
                paths.ToString(),
            }));
        }
    }

    private string DeviceId()
    {
        return (_device.VpsDistributionId ?? _device.DeviceId ?? "").Trim();
    }

    private static bool IsValidDeviceId(string deviceId)
    {
        return deviceId.Length == 6 && deviceId.All(char.IsDigit);
    }

    private void UpdateRemoteSshButton()
    {
        var deviceId = DeviceId();
        _remoteSsh.Text = IsValidDeviceId(deviceId)
            ? $"远程连接：设备 {deviceId}（VPS）"
            : "远程连接：设备 ID 无效";
        _remoteSsh.Enabled = IsValidDeviceId(deviceId);
    }

    private static async Task<(string IdentityFile, string Error)> ResolveBoardIdentityFileAsync(string sshPath)
    {
        try
        {
            var start = new ProcessStartInfo
            {
                FileName = sshPath,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
            };
            start.ArgumentList.Add("-G");
            start.ArgumentList.Add("re-board");
            using var process = Process.Start(start);
            if (process is null)
            {
                return ("", "Windows SSH 配置读取失败。");
            }

            var outputTask = process.StandardOutput.ReadToEndAsync();
            var errorTask = process.StandardError.ReadToEndAsync();
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
            try
            {
                await process.WaitForExitAsync(timeout.Token);
            }
            catch (OperationCanceledException)
            {
                process.Kill(entireProcessTree: true);
                return ("", "读取 Windows SSH 别名 re-board 超时。");
            }
            var output = await outputTask;
            _ = await errorTask;
            if (process.ExitCode != 0)
            {
                return ("", "Windows SSH 别名 re-board 无法解析。");
            }

            foreach (var line in output.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            {
                const string prefix = "identityfile ";
                if (!line.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }
                var candidate = line[prefix.Length..].Trim().Trim('"');
                if (candidate.StartsWith("~/", StringComparison.Ordinal) || candidate.StartsWith("~\\", StringComparison.Ordinal))
                {
                    candidate = Path.Combine(
                        Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
                        candidate[2..].Replace('/', Path.DirectorySeparatorChar));
                }
                candidate = Environment.ExpandEnvironmentVariables(candidate);
                if (File.Exists(candidate))
                {
                    return (Path.GetFullPath(candidate), "");
                }
            }
            return ("", "Windows SSH 别名 re-board 未配置可用的 IdentityFile。");
        }
        catch (Exception exc)
        {
            return ("", "读取 Windows SSH 别名 re-board 失败：" + exc.Message);
        }
    }

    private async Task ConnectRemoteSshAsync()
    {
        var deviceId = DeviceId();
        if (!IsValidDeviceId(deviceId))
        {
            MessageBox.Show(this, "当前设备没有合法的 6 位 VPS 分发 ID。", "远程连接", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }
        if (!string.Equals((_device.AuthorizationStatus ?? "").Trim(), "authorized", StringComparison.OrdinalIgnoreCase))
        {
            MessageBox.Show(this, "当前设备尚未授权，请先重新生成并下发包含远程 SSH 权限的授权文件。", "远程连接", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        var sshPath = Path.Combine(Environment.SystemDirectory, "OpenSSH", "ssh.exe");
        if (!File.Exists(sshPath))
        {
            MessageBox.Show(this, $"Windows 系统 SSH 不存在：{sshPath}", "远程连接", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }
        var boardIdentity = await ResolveBoardIdentityFileAsync(sshPath);
        if (string.IsNullOrWhiteSpace(boardIdentity.IdentityFile))
        {
            MessageBox.Show(this, boardIdentity.Error, "远程连接失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }

        _remoteSsh.Enabled = false;
        _remoteSsh.Text = $"正在查询设备 {deviceId} 隧道…";
        var result = await _api.GetRemoteSshStatusAsync(deviceId, CancellationToken.None);
        UpdateRemoteSshButton();
        if (!result.Success || result.Value is null)
        {
            var error = string.IsNullOrWhiteSpace(result.Error) ? "VPS 未返回远程 SSH 状态。" : result.Error;
            _log($"远程 SSH 状态读取失败：设备 {deviceId} / {error}");
            MessageBox.Show(this, error, "远程连接失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }

        var status = result.Value;
        if (!string.Equals(status.DeviceId, deviceId, StringComparison.Ordinal))
        {
            MessageBox.Show(this, "VPS 返回了其它设备的隧道状态。", "远程连接失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }
        if (!status.Registered || !status.Online)
        {
            var message = string.IsNullOrWhiteSpace(status.Message)
                ? "设备反向 SSH 通道尚未在线，请确认设备联网并已下载最新授权。"
                : status.Message;
            _log($"远程 SSH 未在线：设备 {deviceId}");
            MessageBox.Show(this, message, "设备不在线", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }
        if (status.AssignedPort is null ||
            status.AssignedPort.Value < 25000 ||
            status.AssignedPort.Value > 44999 ||
            status.VpsPort != 22 ||
            !string.Equals(status.VpsHost, "it.cjwsjzyy.xyz", StringComparison.OrdinalIgnoreCase) ||
            !string.Equals(status.TunnelUser, "8ax-tunnel", StringComparison.Ordinal))
        {
            MessageBox.Show(this, "VPS 返回的设备身份或隧道端口信息无效。", "远程连接失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }

        var port = status.AssignedPort.Value;
        var hostKeyAlias = "8ax-device-" + deviceId;
        var script =
            "& '" + sshPath.Replace("'", "''") + "' " +
            "-o 'ConnectTimeout=10' " +
            "-o 'HostKeyAlias=" + hostKeyAlias + "' " +
            "-o 'StrictHostKeyChecking=ask' " +
            "-o 'HostKeyAlgorithms=+ssh-rsa' " +
            "-o 'PubkeyAcceptedAlgorithms=+ssh-rsa' " +
            "-i '" + boardIdentity.IdentityFile.Replace("'", "''") + "' " +
            "-J 'vps3' -p " + port + " 'root@127.0.0.1'";
        var start = new ProcessStartInfo
        {
            FileName = "powershell.exe",
            UseShellExecute = true,
            WorkingDirectory = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
        };
        start.ArgumentList.Add("-NoLogo");
        start.ArgumentList.Add("-NoExit");
        start.ArgumentList.Add("-Command");
        start.ArgumentList.Add(script);
        Process.Start(start);
        _log($"已从 IP访问记录启动远程 SSH：设备 {deviceId} / VPS loopback 端口 {port}。");
    }

    private void LoadDetails()
    {
        _details.Rows.Clear();
        foreach (var item in _records)
        {
            var rowIndex = _details.Rows.Add(
                FormatTime(item.Time),
                Clean(item.Ip),
                StatusText(item),
                Clean(item.Path),
                item.Nonce ?? "-"
            );
            _details.Rows[rowIndex].Tag = item;
        }
    }

    private void LoadPathStats(ListView list)
    {
        list.Items.Clear();
        foreach (var group in _records.GroupBy(item => Clean(item.Path)).OrderByDescending(group => group.Count()).ThenBy(group => group.Key))
        {
            var latest = group.OrderByDescending(item => item.Time).FirstOrDefault();
            list.Items.Add(new ListViewItem(new[]
            {
                group.Key,
                group.Count().ToString(),
                FormatTime(latest?.Time),
            }));
        }
    }

    private void DeleteSelected()
    {
        if (_details.CurrentRow?.Tag is not DeviceIpAccessRecord record || string.IsNullOrWhiteSpace(record.Nonce))
        {
            MessageBox.Show(this, "请先选择一条可删除的 IP 访问记录。", "删除 IP 访问记录", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }

        var confirm = MessageBox.Show(
            this,
            $"确认删除这条 IP 访问记录？\n\n设备：{_device.DeviceId}\n时间：{FormatTime(record.Time)}\nIP：{Clean(record.Ip)}\n状态：{StatusText(record)}",
            "确认删除 IP 访问记录",
            MessageBoxButtons.OKCancel,
            MessageBoxIcon.Warning);
        if (confirm != DialogResult.OK)
        {
            return;
        }

        DeleteNonce = record.Nonce;
        DialogResult = DialogResult.OK;
        Close();
    }

    private static bool IsDownload(DeviceIpAccessRecord item)
    {
        return string.Equals(item.Status, "download", StringComparison.OrdinalIgnoreCase);
    }

    private static string StatusText(DeviceIpAccessRecord item)
    {
        return IsDownload(item) ? "下载" : "挑战";
    }

    private static string FormatTime(DateTimeOffset? value)
    {
        return value?.LocalDateTime.ToString("yyyy-MM-dd HH:mm:ss") ?? "-";
    }

    private static string Clean(string? value)
    {
        return string.IsNullOrWhiteSpace(value) ? "-" : value.Trim();
    }
}
