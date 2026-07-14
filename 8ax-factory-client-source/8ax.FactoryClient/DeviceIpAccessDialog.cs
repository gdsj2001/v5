using System.Diagnostics;
using System.Net;

namespace EightAxis.FactoryClient;

internal sealed class DeviceIpAccessDialog : Form
{
    private readonly DeviceRecord _device;
    private readonly List<DeviceIpAccessRecord> _records;
    private readonly Action<string> _log;
    private ListView _summary = null!;
    private ListView _ipStats = null!;
    private ListView _pathStats = null!;
    private readonly DataGridView _details = new();
    private readonly TextBox _deleteNote = new();
    private Button _remoteSsh = null!;
    private string _remoteSshIp = "";

    public string DeleteNonce { get; private set; } = "";
    public string DeleteNote => _deleteNote.Text.Trim();

    public DeviceIpAccessDialog(DeviceRecord device, Action<string> log)
    {
        _device = device;
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
            Text = "SSH 目标由最近 IP 或当前选中的明细/IP分布行决定",
            Dock = DockStyle.Fill,
            TextAlign = ContentAlignment.MiddleRight,
        };
        _remoteSsh = new Button { Dock = DockStyle.Fill };
        _remoteSsh.Click += (_, _) => LaunchRemoteSsh();
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
        _ipStats.SelectedIndexChanged += (_, _) => SelectIpStatsTarget();
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
        _details.SelectionChanged += (_, _) => SelectDetailTarget();
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
        SetRemoteSshTarget(_records.FirstOrDefault()?.Ip);
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
            var row = new ListViewItem(new[]
            {
                group.Key,
                group.Count().ToString(),
                group.Count(IsDownload).ToString(),
                group.Count(item => !IsDownload(item)).ToString(),
                FormatTime(latest?.Time),
                paths.ToString(),
            });
            row.Tag = group.Key;
            list.Items.Add(row);
        }
    }

    private void SelectDetailTarget()
    {
        if (_details.CurrentRow?.Tag is DeviceIpAccessRecord record)
        {
            SetRemoteSshTarget(record.Ip);
        }
    }

    private void SelectIpStatsTarget()
    {
        if (_ipStats.SelectedItems.Count > 0)
        {
            SetRemoteSshTarget(_ipStats.SelectedItems[0].Tag as string);
        }
    }

    private void SetRemoteSshTarget(string? rawIp)
    {
        _remoteSshIp = IPAddress.TryParse((rawIp ?? "").Trim(), out var address)
            ? address.ToString()
            : "";
        _remoteSsh.Text = string.IsNullOrEmpty(_remoteSshIp)
            ? "远程连接：无有效 IP"
            : "远程连接：" + _remoteSshIp;
        _remoteSsh.Enabled = !string.IsNullOrEmpty(_remoteSshIp);
    }

    private void LaunchRemoteSsh()
    {
        if (!IPAddress.TryParse(_remoteSshIp, out var address))
        {
            MessageBox.Show(this, "当前设备没有可用的 SSH 目标 IP。", "远程连接", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }

        var sshPath = Path.Combine(Environment.SystemDirectory, "OpenSSH", "ssh.exe");
        if (!File.Exists(sshPath))
        {
            MessageBox.Show(this, $"Windows 系统 SSH 不存在：{sshPath}", "远程连接", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }

        var deviceId = (_device.VpsDistributionId ?? _device.DeviceId ?? "").Trim();
        var hostKeyAlias = deviceId.Length == 6 && deviceId.All(char.IsDigit)
            ? "8ax-device-" + deviceId
            : "8ax-device-ip-" + address.ToString().Replace(':', '-');
        var script =
            "& '" + sshPath.Replace("'", "''") + "' " +
            "-o 'ConnectTimeout=10' " +
            "-o 'HostKeyAlias=" + hostKeyAlias + "' " +
            "-o 'StrictHostKeyChecking=ask' " +
            "-p 22 'root@" + address + "'";
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
        _log($"已从设备 {deviceId} 的 IP访问记录启动 SSH：{address}:22。");
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
