namespace EightAxis.FactoryClient;

internal sealed class MainForm : Form
{
    private readonly ApiClient _api = new();
    private readonly LocalSettings _settings = LocalSettings.Load();

    private TextBox _serverUrl = null!;
    private TextBox _adminUser = null!;
    private TextBox _adminPassword = null!;
    private ListView _dealers = null!;
    private ListView _dealerUsers = null!;
    private DataGridView _devices = null!;
    private ListView _upgradeRequests = null!;
    private TextBox _reviewNote = null!;
    private TextBox _deviceNote = null!;
    private TextBox _upgradeReviewNote = null!;
    private TextBox _requestInput = null!;
    private ListView _parsed = null!;
    private TextBox _log = null!;

    public MainForm()
    {
        Text = "8ax 厂家授权控制台";
        AutoScaleMode = AutoScaleMode.Dpi;
        AutoScroll = true;
        ClientSize = new Size(1600, 1200);
        MinimumSize = new Size(1180, 760);
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
            RowCount = 3,
            Padding = new Padding(14),
        };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 112));
        Controls.Add(root);

        root.Controls.Add(BuildHeader(), 0, 0);

        var tabs = new TabControl { Dock = DockStyle.Fill };
        tabs.TabPages.Add(CreateTab("经销商 / 员工", BuildDealerReviewPanel()));
        tabs.TabPages.Add(CreateTab("设备DNA", BuildDevicePanel()));
        tabs.TabPages.Add(CreateTab("升级请求", BuildRequestPanel()));
        tabs.TabPages.Add(CreateTab("驱动发布", new DriveProfilePublishPanel(() => { ApplySettings(); return _api; }, Log)));
        tabs.TabPages.Add(CreateTab("OTA发布", new OtaPublishPanel(() => { ApplySettings(); return _api; }, Log)));
        root.Controls.Add(tabs, 0, 1);
        root.Controls.Add(BuildLogPanel(), 0, 2);
    }

    private static TabPage CreateTab(string title, Control content)
    {
        var page = new TabPage(title) { Padding = new Padding(8) };
        page.Controls.Add(content);
        return page;
    }

    private Control BuildHeader()
    {
        var panel = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 1, RowCount = 2 };
        panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));

        var title = new Label
        {
            Text = "8ax 厂家授权控制台",
            Dock = DockStyle.Fill,
            Font = new Font(Font.FontFamily, 18F, FontStyle.Bold),
            TextAlign = ContentAlignment.MiddleLeft,
            AutoEllipsis = true,
        };
        var version = new Label
        {
            Text = $"版本 {ClientInfo.Version} / {ClientInfo.Build}",
            AutoSize = true,
            Anchor = AnchorStyles.Right,
            TextAlign = ContentAlignment.MiddleRight,
        };

        var titleRow = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2 };
        titleRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        titleRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 300));
        titleRow.Controls.Add(title, 0, 0);
        titleRow.Controls.Add(version, 1, 0);
        panel.Controls.Add(titleRow, 0, 0);

        _serverUrl = new TextBox { Dock = DockStyle.Fill };
        _adminUser = new TextBox { Dock = DockStyle.Fill };
        _adminPassword = new TextBox { Dock = DockStyle.Fill, UseSystemPasswordChar = true };

        var form = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 8, RowCount = 2 };
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 72));
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 35));
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 86));
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 25));
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 86));
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 40));
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 120));
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 130));
        form.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        form.RowStyles.Add(new RowStyle(SizeType.AutoSize));

        form.Controls.Add(new Label { Text = "服务器", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, 0);
        form.Controls.Add(_serverUrl, 1, 0);
        form.SetColumnSpan(_serverUrl, 5);
        form.Controls.Add(new Label { Text = "厂家账号", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, 1);
        form.Controls.Add(_adminUser, 1, 1);
        form.Controls.Add(new Label { Text = "厂家密码", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 2, 1);
        form.Controls.Add(_adminPassword, 3, 1);
        form.SetColumnSpan(_adminPassword, 3);

        var health = new Button { Text = "连接检查", Dock = DockStyle.Fill, MinimumSize = new Size(0, 32) };
        health.Click += async (_, _) => await RunGuarded(CheckHealthAsync);
        var load = new Button { Text = "刷新全部", Dock = DockStyle.Fill, MinimumSize = new Size(0, 32) };
        load.Click += async (_, _) => await RunGuarded(LoadDealersAsync);
        form.Controls.Add(health, 6, 1);
        form.Controls.Add(load, 7, 1);
        panel.Controls.Add(form, 0, 1);

        return panel;
    }

    private Control BuildDealerReviewPanel()
    {
        var group = new GroupBox { Text = "经销商审核 / 员工账号", Dock = DockStyle.Fill };
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 2, ColumnCount = 2, Padding = new Padding(10) };
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 68));
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 32));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 44));
        group.Controls.Add(root);

        _dealers = new ListView
        {
            Dock = DockStyle.Fill,
            View = View.Details,
            FullRowSelect = true,
            GridLines = true,
            HideSelection = false,
        };
        _dealers.Columns.Add("编号", 64);
        _dealers.Columns.Add("提交时间", 145);
        _dealers.Columns.Add("用户名", 110);
        _dealers.Columns.Add("审核", 110);
        _dealers.Columns.Add("合作", 90);
        _dealers.Columns.Add("厂家联系", 220);
        _dealers.Columns.Add("终端联系", 220);
        _dealers.Columns.Add("经销商ID", 260);
        _dealers.SelectedIndexChanged += (_, _) => UpdateSelectedDealerHint();
        _dealers.Resize += (_, _) => AdjustDealerColumns();
        root.Controls.Add(_dealers, 0, 0);

        _dealerUsers = new ListView
        {
            Dock = DockStyle.Fill,
            View = View.Details,
            FullRowSelect = true,
            GridLines = true,
            HideSelection = false,
        };
        _dealerUsers.Columns.Add("编号", 64);
        _dealerUsers.Columns.Add("员工账号", 110);
        _dealerUsers.Columns.Add("姓名", 100);
        _dealerUsers.Columns.Add("状态", 80);
        _dealerUsers.Columns.Add("电话", 120);
        root.Controls.Add(_dealerUsers, 1, 0);

        var actions = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 5 };
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 110));
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 110));
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 120));
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 120));
        _reviewNote = new TextBox { Dock = DockStyle.Fill, PlaceholderText = "审核备注，可空" };
        var approve = new Button { Text = "通过", Dock = DockStyle.Fill };
        approve.Click += async (_, _) => await RunGuarded(() => ReviewSelectedDealerAsync("approved"));
        var reject = new Button { Text = "拒绝", Dock = DockStyle.Fill };
        reject.Click += async (_, _) => await RunGuarded(() => ReviewSelectedDealerAsync("rejected"));
        var refresh = new Button { Text = "刷新", Dock = DockStyle.Fill };
        refresh.Click += async (_, _) => await RunGuarded(LoadDealersAsync);
        var deleteDealer = new Button { Text = "禁用经销商", Dock = DockStyle.Fill };
        deleteDealer.Click += async (_, _) => await RunGuarded(DeleteSelectedDealerAsync);
        actions.Controls.Add(_reviewNote, 0, 0);
        actions.Controls.Add(approve, 1, 0);
        actions.Controls.Add(reject, 2, 0);
        actions.Controls.Add(refresh, 3, 0);
        actions.Controls.Add(deleteDealer, 4, 0);
        root.Controls.Add(actions, 0, 1);
        root.SetColumnSpan(actions, 2);

        return group;
    }

    private Control BuildDevicePanel()
    {
        var group = new GroupBox { Text = "已登记设备 DNA / 授权门禁", Dock = DockStyle.Fill };
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 2, ColumnCount = 1, Padding = new Padding(10) };
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
        group.Controls.Add(root);

        _devices = new DataGridView
        {
            Dock = DockStyle.Fill,
            AllowUserToAddRows = false,
            AllowUserToDeleteRows = false,
            AllowUserToResizeRows = false,
            AutoGenerateColumns = false,
            BackgroundColor = SystemColors.Window,
            BorderStyle = BorderStyle.FixedSingle,
            ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.AutoSize,
            EditMode = DataGridViewEditMode.EditOnEnter,
            GridColor = SystemColors.ControlLight,
            MultiSelect = false,
            ReadOnly = false,
            RowHeadersVisible = false,
            SelectionMode = DataGridViewSelectionMode.FullRowSelect,
        };
        AddDeviceTextColumn("status", "状态", 90);
        AddDeviceTextColumn("registeredAt", "登记时间", 135);
        AddDeviceTextColumn("plDna", "PL DNA", 160);
        AddDeviceTextColumn("dnaHash", "DNA摘要", 210);
        AddDeviceTextColumn("vpsDistributionId", "VPS分发ID", 95);
        AddDeviceTextColumn("publicKeyHash", "公钥指纹", 210);
        AddDeviceTextColumn("source", "来源", 125);
        AddDeviceTextColumn("deviceId", "设备ID", 85);
        var ipColumn = new DataGridViewButtonColumn
        {
            Name = "ipAccess",
            HeaderText = "IP访问记录",
            Width = 180,
            MinimumWidth = 150,
            FlatStyle = FlatStyle.System,
            ReadOnly = false,
            SortMode = DataGridViewColumnSortMode.NotSortable,
        };
        _devices.Columns.Add(ipColumn);
        AddDeviceTextColumn("version", "版本", 95);
        AddDeviceTextColumn("updatedAt", "更新时间", 135);
        _devices.CellContentClick += Devices_CellContentClick;
        root.Controls.Add(_devices, 0, 0);

        var actions = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 4 };
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 110));
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 120));
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 130));
        _deviceNote = new TextBox { Dock = DockStyle.Fill, PlaceholderText = "删除原因，可空；只允许删除未出库、未绑定的 factory_registered 设备" };
        var refresh = new Button { Text = "刷新DNA", Dock = DockStyle.Fill };
        refresh.Click += async (_, _) => await RunGuarded(LoadDevicesAsync);
        var authorize = new Button { Text = "人工确认授权", Dock = DockStyle.Fill };
        authorize.Click += async (_, _) => await RunGuarded(GenerateSelectedDeviceAuthorizationAsync);
        var delete = new Button { Text = "删除登记", Dock = DockStyle.Fill };
        delete.Click += async (_, _) => await RunGuarded(DeleteSelectedDeviceAsync);
        actions.Controls.Add(_deviceNote, 0, 0);
        actions.Controls.Add(refresh, 1, 0);
        actions.Controls.Add(authorize, 2, 0);
        actions.Controls.Add(delete, 3, 0);
        root.Controls.Add(actions, 0, 1);

        return group;
    }

    private void AddDeviceTextColumn(string name, string header, int width)
    {
        _devices.Columns.Add(new DataGridViewTextBoxColumn
        {
            Name = name,
            HeaderText = header,
            Width = width,
            ReadOnly = true,
            SortMode = DataGridViewColumnSortMode.NotSortable,
        });
    }

    private Control BuildRequestPanel()
    {
        var group = new GroupBox { Text = "终端升级请求 / 二维码内容解析", Dock = DockStyle.Fill };
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, Padding = new Padding(10) };
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 56));
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 44));
        group.Controls.Add(root);

        var left = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 2 };
        left.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        left.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
        _upgradeRequests = new ListView
        {
            Dock = DockStyle.Fill,
            View = View.Details,
            FullRowSelect = true,
            GridLines = true,
            HideSelection = false,
        };
        _upgradeRequests.Columns.Add("编号", 64);
        _upgradeRequests.Columns.Add("时间", 120);
        _upgradeRequests.Columns.Add("设备", 130);
        _upgradeRequests.Columns.Add("经销商", 110);
        _upgradeRequests.Columns.Add("员工", 100);
        _upgradeRequests.Columns.Add("状态", 90);
        _upgradeRequests.Columns.Add("请求ID", 220);
        left.Controls.Add(_upgradeRequests, 0, 0);

        var review = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 5 };
        review.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        review.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 92));
        review.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 92));
        review.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 92));
        review.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 92));
        _upgradeReviewNote = new TextBox { Dock = DockStyle.Fill, PlaceholderText = "终端请求审批备注，可空" };
        var approve = new Button { Text = "通过", Dock = DockStyle.Fill };
        approve.Click += async (_, _) => await RunGuarded(() => ReviewSelectedUpgradeRequestAsync("approved"));
        var reject = new Button { Text = "拒绝", Dock = DockStyle.Fill };
        reject.Click += async (_, _) => await RunGuarded(() => ReviewSelectedUpgradeRequestAsync("rejected"));
        var delete = new Button { Text = "删除", Dock = DockStyle.Fill };
        delete.Click += async (_, _) => await RunGuarded(DeleteSelectedUpgradeRequestAsync);
        var refresh = new Button { Text = "刷新", Dock = DockStyle.Fill };
        refresh.Click += async (_, _) => await RunGuarded(LoadUpgradeRequestsAsync);
        review.Controls.Add(_upgradeReviewNote, 0, 0);
        review.Controls.Add(approve, 1, 0);
        review.Controls.Add(reject, 2, 0);
        review.Controls.Add(delete, 3, 0);
        review.Controls.Add(refresh, 4, 0);
        left.Controls.Add(review, 0, 1);
        root.Controls.Add(left, 0, 0);

        var right = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 2 };
        right.RowStyles.Add(new RowStyle(SizeType.Percent, 48));
        right.RowStyles.Add(new RowStyle(SizeType.Percent, 52));
        _requestInput = new TextBox
        {
            Dock = DockStyle.Fill,
            Multiline = true,
            ScrollBars = ScrollBars.Vertical,
            PlaceholderText = "粘贴设备二维码内容、JSON 或经销商发来的升级申请文本。",
        };
        var parse = new Button { Text = "解析申请内容", Dock = DockStyle.Right, Width = 130 };
        parse.Click += (_, _) => ParseRequestText();
        var parseBox = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 2 };
        parseBox.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        parseBox.RowStyles.Add(new RowStyle(SizeType.Absolute, 38));
        parseBox.Controls.Add(_requestInput, 0, 0);
        parseBox.Controls.Add(parse, 0, 1);
        right.Controls.Add(parseBox, 0, 0);

        _parsed = new ListView
        {
            Dock = DockStyle.Fill,
            View = View.Details,
            GridLines = true,
            FullRowSelect = true,
        };
        _parsed.Columns.Add("字段", 160);
        _parsed.Columns.Add("内容", 360);
        right.Controls.Add(_parsed, 0, 1);
        root.Controls.Add(right, 1, 0);

        return group;
    }

    private Control BuildLogPanel()
    {
        var group = new GroupBox { Text = "运行日志", Dock = DockStyle.Fill };
        _log = new TextBox { Dock = DockStyle.Fill, Multiline = true, ScrollBars = ScrollBars.Vertical, ReadOnly = true };
        group.Controls.Add(_log);
        return group;
    }

    private void AdjustDealerColumns()
    {
        if (_dealers.Columns.Count < 8 || _dealers.ClientSize.Width <= 0)
        {
            return;
        }

        var width = Math.Max(820, _dealers.ClientSize.Width - SystemInformation.VerticalScrollBarWidth - 8);
        _dealers.Columns[0].Width = 64;
        _dealers.Columns[1].Width = Math.Max(120, (int)(width * 0.14));
        _dealers.Columns[2].Width = Math.Max(92, (int)(width * 0.11));
        _dealers.Columns[3].Width = Math.Max(84, (int)(width * 0.10));
        _dealers.Columns[4].Width = Math.Max(76, (int)(width * 0.08));
        _dealers.Columns[5].Width = Math.Max(170, (int)(width * 0.20));
        _dealers.Columns[6].Width = Math.Max(170, (int)(width * 0.20));
        _dealers.Columns[7].Width = Math.Max(190, width - _dealers.Columns[0].Width - _dealers.Columns[1].Width - _dealers.Columns[2].Width - _dealers.Columns[3].Width - _dealers.Columns[4].Width - _dealers.Columns[5].Width - _dealers.Columns[6].Width);
    }

    private void LoadSettings()
    {
        _serverUrl.Text = _settings.ServerUrl;
        _adminUser.Text = _settings.AdminUsername;
        _adminPassword.Text = LocalSettings.DefaultAdminPassword;
        Log("厂家授权控制台启动。");
    }

    private void ApplySettings()
    {
        _settings.ServerUrl = string.IsNullOrWhiteSpace(_serverUrl.Text) ? LocalSettings.PrimaryServerUrl : _serverUrl.Text.Trim();
        _settings.AdminUsername = _adminUser.Text.Trim();
        _settings.Save();

        _api.BaseUrl = _settings.ServerUrl;
        _api.AdminUsername = _settings.AdminUsername;
        _api.AdminPassword = _adminPassword.Text;
    }

    private async Task CheckHealthAsync()
    {
        ApplySettings();
        var result = await _api.HealthAsync(CancellationToken.None);
        if (!result.Success)
        {
            Log(result.Error);
            return;
        }

        Log($"连接正常：{result.Value?.Service} / {result.Value?.Host} / {result.Value?.Time}");
    }

    private async Task LoadDealersAsync()
    {
        ApplySettings();
        if (string.IsNullOrWhiteSpace(_api.AdminUsername) || string.IsNullOrWhiteSpace(_api.AdminPassword))
        {
            Log("请填写厂家账号和密码。");
            return;
        }

        var result = await _api.GetDealersAsync(CancellationToken.None);
        if (!result.Success)
        {
            Log(result.Error);
            return;
        }

        _dealers.Items.Clear();
        foreach (var dealer in result.Value?.Dealers ?? [])
        {
            var item = new ListViewItem(new[]
            {
                dealer.DealerNo ?? "-",
                dealer.CreatedAt?.LocalDateTime.ToString("yyyy-MM-dd HH:mm") ?? "-",
                dealer.Username,
                dealer.ReviewStatus ?? "-",
                dealer.CooperationStatus ?? "-",
                $"{dealer.ContactName} / {dealer.Phone} / {dealer.Wechat}",
                $"{dealer.CustomerContactName} / {dealer.CustomerPhone} / {dealer.CustomerWechat}",
                dealer.DealerId,
            })
            {
                Tag = dealer,
            };
            _dealers.Items.Add(item);
        }

        Log($"已刷新经销商记录：{_dealers.Items.Count} 条。");
        await LoadDealerUsersAsync(GetSelectedDealerId());
        await LoadDevicesAsync();
        await LoadUpgradeRequestsAsync();
    }

    private async Task ReviewSelectedDealerAsync(string decision)
    {
        ApplySettings();
        if (_dealers.SelectedItems.Count == 0 || _dealers.SelectedItems[0].Tag is not DealerRecord dealer)
        {
            Log("请先选择一条经销商记录。");
            return;
        }

        var label = decision == "approved" ? "通过" : "拒绝";
        var confirm = MessageBox.Show(this, $"确认{label}经销商账号 {dealer.Username}？", "确认审核", MessageBoxButtons.OKCancel, MessageBoxIcon.Question);
        if (confirm != DialogResult.OK)
        {
            return;
        }

        var result = await _api.ReviewDealerAsync(new DealerReviewRequest(dealer.DealerId, decision, _reviewNote.Text.Trim()), CancellationToken.None);
        if (!result.Success)
        {
            Log(result.Error);
            return;
        }

        Log($"审核完成：{result.Value?.Username ?? dealer.Username} -> {result.Value?.ReviewStatus ?? decision}");
        await LoadDealersAsync();
    }

    private void UpdateSelectedDealerHint()
    {
        if (_dealers.SelectedItems.Count == 0 || _dealers.SelectedItems[0].Tag is not DealerRecord dealer)
        {
            return;
        }

        _reviewNote.Text = dealer.ReviewNote ?? "";
        _ = RunGuarded(() => LoadDealerUsersAsync(dealer.DealerId));
    }

    private string? GetSelectedDealerId()
    {
        return _dealers.SelectedItems.Count > 0 && _dealers.SelectedItems[0].Tag is DealerRecord dealer ? dealer.DealerId : null;
    }

    private async Task LoadDealerUsersAsync(string? dealerId)
    {
        ApplySettings();
        var result = await _api.GetDealerUsersAsync(dealerId, CancellationToken.None);
        if (!result.Success)
        {
            Log(result.Error);
            return;
        }

        _dealerUsers.Items.Clear();
        foreach (var user in result.Value?.Users ?? [])
        {
            var item = new ListViewItem(new[]
            {
                user.DealerUserNo ?? "-",
                user.Username,
                user.DisplayName ?? "-",
                user.Status ?? "-",
                user.Phone ?? "-",
            })
            {
                Tag = user,
            };
            _dealerUsers.Items.Add(item);
        }

        Log($"已刷新员工账号：{_dealerUsers.Items.Count} 条。");
    }

    private async Task LoadDevicesAsync()
    {
        ApplySettings();
        var result = await _api.GetDevicesAsync(CancellationToken.None);
        if (!result.Success)
        {
            Log(result.Error);
            return;
        }

        _devices.Rows.Clear();
        foreach (var device in result.Value?.Devices ?? [])
        {
            var rowIndex = _devices.Rows.Add(
                DeviceStatusText(device),
                device.FactoryRegisteredAt?.LocalDateTime.ToString("yyyy-MM-dd HH:mm") ?? "-",
                device.PlDeviceDna ?? "-",
                device.PlDnaHash ?? "-",
                FormatVpsDistributionId(device.VpsDistributionId),
                device.DevicePublicKeySha256 ?? "-",
                device.DeviceIdSource ?? "-",
                device.DeviceId,
                BuildIpAccessButtonText(device),
                device.CurrentVersion ?? device.InitialVersion ?? "-",
                device.UpdatedAt?.LocalDateTime.ToString("yyyy-MM-dd HH:mm") ?? "-"
            );
            var row = _devices.Rows[rowIndex];
            row.Tag = device;
            ApplyDeviceAuthorizationStatusStyle(row, device);
        }

        Log($"已刷新设备 DNA 登记：{_devices.Rows.Count} 条。");
    }

    private static string FormatVpsDistributionId(string? value)
    {
        var text = (value ?? "").Trim();
        return text.Length == 6 && text.All(char.IsDigit) ? text : "-";
    }

    private static string DeviceStatusText(DeviceRecord device)
    {
        if (IsPendingFactoryAuthorization(device))
        {
            return "待人工授权";
        }

        if (IsDeviceAuthorized(device))
        {
            return "已授权";
        }

        return device.ActivationStatus ?? "-";
    }

    private static bool IsPendingFactoryAuthorization(DeviceRecord device)
    {
        var status = (device.AuthorizationStatus ?? "").Trim();
        return status.Equals("pending_factory_authorization", StringComparison.OrdinalIgnoreCase)
            || status.Equals("pending_factory_upload", StringComparison.OrdinalIgnoreCase)
            || status.Equals("pending", StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsDeviceAuthorized(DeviceRecord device)
    {
        return string.Equals(
            (device.AuthorizationStatus ?? "").Trim(),
            "authorized",
            StringComparison.OrdinalIgnoreCase);
    }

    private static void ApplyDeviceAuthorizationStatusStyle(DataGridViewRow row, DeviceRecord device)
    {
        if (row.DataGridView is null || !row.DataGridView.Columns.Contains("status"))
        {
            return;
        }

        var cell = row.Cells["status"];
        if (IsPendingFactoryAuthorization(device))
        {
            cell.Style.BackColor = Color.MistyRose;
            cell.Style.ForeColor = Color.Red;
            cell.Style.SelectionBackColor = Color.MistyRose;
            cell.Style.SelectionForeColor = Color.Red;
            return;
        }

        if (IsDeviceAuthorized(device))
        {
            cell.Style.ForeColor = Color.DarkGreen;
            cell.Style.SelectionForeColor = Color.DarkGreen;
        }
    }

    private async void Devices_CellContentClick(object? sender, DataGridViewCellEventArgs e)
    {
        if (e.RowIndex < 0 || e.ColumnIndex < 0 || _devices.Columns[e.ColumnIndex].Name != "ipAccess")
        {
            return;
        }

        if (_devices.Rows[e.RowIndex].Tag is not DeviceRecord device)
        {
            return;
        }

        using var dialog = new DeviceIpAccessDialog(device);
        if (dialog.ShowDialog(this) == DialogResult.OK && !string.IsNullOrWhiteSpace(dialog.DeleteNonce))
        {
            await RunGuarded(() => DeleteDeviceIpAccessAsync(device, dialog.DeleteNonce, dialog.DeleteNote));
        }
    }

    private static string BuildIpAccessButtonText(DeviceRecord device)
    {
        var records = device.IpAccessRecords ?? [];
        if (records.Count == 0)
        {
            return "查看 0 条";
        }

        var latest = records.OrderByDescending(item => item.Time).FirstOrDefault();
        var ip = string.IsNullOrWhiteSpace(latest?.Ip) ? "-" : latest.Ip.Trim();
        return $"查看 {records.Count} 条 / {ip}";
    }

    private async Task DeleteDeviceIpAccessAsync(DeviceRecord device, string nonce, string note)
    {
        ApplySettings();
        var result = await _api.DeleteAsync(new DeleteRequest("device_ip_access", nonce, note), CancellationToken.None);
        if (!result.Success)
        {
            Log(result.Error);
            return;
        }

        Log(result.Value?.Message ?? $"设备 {device.DeviceId} 的 IP 访问记录已删除。");
        await LoadDevicesAsync();
    }

    private async Task GenerateSelectedDeviceAuthorizationAsync()
    {
        ApplySettings();
        var selectedRow = _devices.CurrentRow;
        if (selectedRow?.Tag is not DeviceRecord device)
        {
            const string message = "请先选择一条设备 DNA 登记。";
            Log(message);
            ShowDeviceAuthorizationMessage("未选择设备", message, MessageBoxIcon.Warning);
            return;
        }

        if (string.IsNullOrWhiteSpace(device.PlDnaHash) || string.IsNullOrWhiteSpace(device.DevicePublicKeySha256))
        {
            const string message = "该设备缺少 DNA 摘要或设备公钥指纹，不能生成授权。";
            Log(message);
            ShowDeviceAuthorizationMessage("授权生成失败", message, MessageBoxIcon.Error);
            return;
        }

        if (!ConfirmDeviceAuthorization(device))
        {
            Log("已取消设备授权生成；未签名、未上传。");
            return;
        }

        var privateKeyPath = Path.Combine(AppContext.BaseDirectory, "factory-device-auth-private.pem");
        if (!File.Exists(privateKeyPath))
        {
            var message = $"缺少工厂授权私钥文件：{privateKeyPath}";
            Log(message);
            ShowDeviceAuthorizationMessage("授权生成失败", message, MessageBoxIcon.Error);
            return;
        }

        Form? progress = null;
        try
        {
            progress = ShowDeviceAuthorizationProgress(device);

            var (envelope, signatureHash) = BuildDeviceAuthorizationEnvelope(device, privateKeyPath);
            var result = await _api.UploadDeviceAuthorizationAsync(
                new DeviceAuthorizationUploadRequest(device.DeviceId, device.PlDnaHash, envelope),
                CancellationToken.None);

            progress.Close();
            if (!result.Success)
            {
                var message = "授权文件上传失败：" + result.Error;
                Log(message);
                ShowDeviceAuthorizationMessage("授权上传失败", message, MessageBoxIcon.Error);
                return;
            }

            var signatureShort = signatureHash[..Math.Min(12, signatureHash.Length)];
            var successMessage =
                (result.Value?.Message ?? "授权文件已上传到 VPS。") + "\n\n" +
                $"设备ID: {device.DeviceId}\n" +
                $"PL DNA摘要: {device.PlDnaHash}\n" +
                $"签名摘要: {signatureShort}\n\n" +
                "板端现在可以点击“下载授权”获取并校验授权文件。";
            Log(result.Value?.Message ?? $"授权文件已上传：{device.DeviceId} / {signatureShort}");
            ShowDeviceAuthorizationMessage("授权上传成功", successMessage, MessageBoxIcon.Information);
            await LoadDevicesAsync();
        }
        catch (Exception ex)
        {
            progress?.Close();
            var message = "授权文件生成或上传失败：" + ex.Message;
            Log(message);
            ShowDeviceAuthorizationMessage("授权生成失败", message, MessageBoxIcon.Error);
            return;
        }
        finally
        {
            progress?.Dispose();
        }
    }

    private Form ShowDeviceAuthorizationProgress(DeviceRecord device)
    {
        var progress = new Form
        {
            Text = "授权生成中",
            Width = 480,
            Height = 170,
            StartPosition = FormStartPosition.CenterParent,
            FormBorderStyle = FormBorderStyle.FixedDialog,
            ControlBox = false,
            MinimizeBox = false,
            MaximizeBox = false,
            ShowInTaskbar = false,
        };
        var label = new Label
        {
            Dock = DockStyle.Fill,
            Padding = new Padding(18),
            TextAlign = ContentAlignment.MiddleLeft,
            Text =
                "正在生成设备授权文件并上传 VPS，请稍候...\n\n" +
                $"设备ID: {device.DeviceId}\n" +
                $"PL DNA摘要: {device.PlDnaHash}",
        };
        progress.Controls.Add(label);
        progress.Show(this);
        progress.Refresh();
        Application.DoEvents();
        return progress;
    }

    private void ShowDeviceAuthorizationMessage(string title, string message, MessageBoxIcon icon)
    {
        MessageBox.Show(this, message, title, MessageBoxButtons.OK, icon);
    }

    private bool ConfirmDeviceAuthorization(DeviceRecord device)
    {
        var message =
            "请人工确认以下 VPS DNA 登记记录属于当前要授权的 8ax 设备。\n\n" +
            $"设备ID: {device.DeviceId}\n" +
            $"登记状态: {device.ActivationStatus ?? "-"}\n" +
            $"登记时间: {device.FactoryRegisteredAt?.LocalDateTime.ToString("yyyy-MM-dd HH:mm:ss") ?? "-"}\n" +
            $"PL DNA摘要: {device.PlDnaHash ?? "-"}\n" +
            $"设备公钥指纹: {device.DevicePublicKeySha256 ?? "-"}\n\n" +
            "确认后工厂客户端会使用本机私钥生成授权文件，并上传 VPS 供板端下载；取消则不会签名、不会上传。";
        var confirm = MessageBox.Show(
            this,
            message,
            "人工确认生成设备授权",
            MessageBoxButtons.OKCancel,
            MessageBoxIcon.Warning,
            MessageBoxDefaultButton.Button2);
        return confirm == DialogResult.OK;
    }

    private static (object Envelope, string SignatureHash) BuildDeviceAuthorizationEnvelope(DeviceRecord device, string privateKeyPath)
    {
        using var rsa = System.Security.Cryptography.RSA.Create();
        rsa.ImportFromPem(File.ReadAllText(privateKeyPath));
        var publicDer = rsa.ExportSubjectPublicKeyInfo();
        var keyHash = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(publicDer)).ToLowerInvariant();
        var keyId = "rsa-sha256:" + keyHash[..16];
        var issuedAt = DateTimeOffset.UtcNow;
        var now = issuedAt.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'");
        var notBefore = issuedAt.AddDays(-30).ToString("yyyy-MM-dd'T'HH:mm:ss'Z'");
        var expires = issuedAt.AddDays(3650).ToString("yyyy-MM-dd'T'HH:mm:ss'Z'");
        var payload = new SortedDictionary<string, object?>
        {
            ["activation_status"] = device.ActivationStatus ?? "factory_registered",
            ["audience"] = "8ax-board-vps-access",
            ["device_id"] = device.DeviceId,
            ["device_id_source"] = device.DeviceIdSource ?? "",
            ["device_public_key_sha256"] = device.DevicePublicKeySha256 ?? "",
            ["expires_at"] = expires,
            ["issued_at"] = now,
            ["issuer"] = "8ax-factory-client",
            ["key_id"] = keyId,
            ["license_anchor_type"] = "zynq7000_pl_device_dna_57",
            ["not_before"] = notBefore,
            ["permissions"] = new[] { "drive_profile_download" },
            ["pl_device_dna_hash"] = device.PlDnaHash ?? "",
            ["schema"] = "8ax-device-authorization-v1",
            ["signature_alg"] = "RSASSA-PKCS1-v1_5-SHA256",
        };
        var canonical = CanonicalJsonBytes(payload);
        var signature = rsa.SignData(
            canonical,
            System.Security.Cryptography.HashAlgorithmName.SHA256,
            System.Security.Cryptography.RSASignaturePadding.Pkcs1);
        var signatureText = Base64Url(signature);
        var envelope = new SortedDictionary<string, object?>
        {
            ["payload"] = payload,
            ["schema"] = "8ax-device-authorization-envelope-v1",
            ["signature"] = new SortedDictionary<string, object?>
            {
                ["alg"] = "RSASSA-PKCS1-v1_5-SHA256",
                ["key_id"] = keyId,
                ["value"] = signatureText,
            },
        };
        var signatureHash = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.ASCII.GetBytes(signatureText))).ToLowerInvariant();
        return (envelope, signatureHash);
    }

    private static byte[] CanonicalJsonBytes(object? value)
    {
        return System.Text.Encoding.UTF8.GetBytes(CanonicalJson(value));
    }

    private static string CanonicalJson(object? value)
    {
        if (value is null)
        {
            return "null";
        }
        if (value is string text)
        {
            return System.Text.Json.JsonSerializer.Serialize(text);
        }
        if (value is bool flag)
        {
            return flag ? "true" : "false";
        }
        if (value is IEnumerable<KeyValuePair<string, object?>> genericDict)
        {
            var items = new List<string>();
            foreach (var item in genericDict.OrderBy(item => item.Key, StringComparer.Ordinal))
            {
                items.Add(CanonicalJson(item.Key) + ":" + CanonicalJson(item.Value));
            }
            return "{" + string.Join(",", items) + "}";
        }
        if (value is System.Collections.IDictionary dict)
        {
            var items = new List<string>();
            foreach (System.Collections.DictionaryEntry item in dict)
            {
                items.Add(CanonicalJson(Convert.ToString(item.Key) ?? "") + ":" + CanonicalJson(item.Value));
            }
            return "{" + string.Join(",", items) + "}";
        }
        if (value is System.Collections.IEnumerable seq && value is not string)
        {
            var items = new List<string>();
            foreach (var item in seq)
            {
                items.Add(CanonicalJson(item));
            }
            return "[" + string.Join(",", items) + "]";
        }
        return System.Text.Json.JsonSerializer.Serialize(value);
    }

    private static string Base64Url(byte[] data)
    {
        return Convert.ToBase64String(data).TrimEnd('=').Replace('+', '-').Replace('/', '_');
    }

    private async Task DeleteSelectedDeviceAsync()
    {
        ApplySettings();
        var selectedRow = _devices.CurrentRow;
        if (selectedRow?.Tag is not DeviceRecord device)
        {
            Log("请先选择一条设备 DNA 登记。");
            return;
        }

        var label = device.PlDeviceDna ?? device.DeviceId;
        var confirm = MessageBox.Show(this, $"确认删除设备 DNA 登记 {label}？\n\n只允许删除未出库、未绑定的 factory_registered 设备。", "确认删除 DNA 登记", MessageBoxButtons.OKCancel, MessageBoxIcon.Warning);
        if (confirm != DialogResult.OK)
        {
            return;
        }

        var result = await _api.DeleteAsync(new DeleteRequest("device", device.DeviceId, _deviceNote.Text.Trim()), CancellationToken.None);
        if (!result.Success)
        {
            Log(result.Error);
            return;
        }

        var message = result.Value?.Message ?? "设备 DNA 登记已删除。";
        if (result.Value?.AuthorizationDeleted == true && !message.Contains("授权"))
        {
            message += " 已同步删除授权文件。";
        }
        Log(message);
        await LoadDevicesAsync();
    }

    private async Task LoadUpgradeRequestsAsync()
    {
        ApplySettings();
        var result = await _api.GetUpgradeRequestsAsync(CancellationToken.None);
        if (!result.Success)
        {
            Log(result.Error);
            return;
        }

        _upgradeRequests.Items.Clear();
        foreach (var req in result.Value?.Requests ?? [])
        {
            var item = new ListViewItem(new[]
            {
                req.UpgradeRequestNo ?? "-",
                req.CreatedAt?.LocalDateTime.ToString("MM-dd HH:mm") ?? "-",
                req.DeviceId ?? "-",
                req.DealerUsername ?? req.DealerName ?? "-",
                req.EmployeeUsername ?? req.EmployeeName ?? "-",
                req.Status ?? "-",
                req.UpgradeRequestId,
            })
            {
                Tag = req,
            };
            _upgradeRequests.Items.Add(item);
        }

        Log($"已刷新终端升级请求：{_upgradeRequests.Items.Count} 条。");
    }

    private async Task ReviewSelectedUpgradeRequestAsync(string decision)
    {
        ApplySettings();
        if (_upgradeRequests.SelectedItems.Count == 0 || _upgradeRequests.SelectedItems[0].Tag is not UpgradeRequestRecord request)
        {
            Log("请先选择一条终端升级请求。");
            return;
        }

        var result = await _api.ReviewUpgradeRequestAsync(new UpgradeRequestReviewRequest(request.UpgradeRequestId, decision, _upgradeReviewNote.Text.Trim()), CancellationToken.None);
        if (!result.Success)
        {
            Log(result.Error);
            return;
        }

        Log(result.Value?.Message ?? "终端升级请求状态已更新。");
        await LoadUpgradeRequestsAsync();
    }

    private async Task DeleteSelectedUpgradeRequestAsync()
    {
        ApplySettings();
        if (_upgradeRequests.SelectedItems.Count == 0 || _upgradeRequests.SelectedItems[0].Tag is not UpgradeRequestRecord request)
        {
            Log("请先选择一条终端升级请求。");
            return;
        }

        var confirm = MessageBox.Show(this, $"确认删除终端升级请求 {request.UpgradeRequestNo ?? request.UpgradeRequestId}？", "确认删除", MessageBoxButtons.OKCancel, MessageBoxIcon.Warning);
        if (confirm != DialogResult.OK)
        {
            return;
        }

        var result = await _api.DeleteAsync(new DeleteRequest("upgrade_request", request.UpgradeRequestId, _upgradeReviewNote.Text.Trim()), CancellationToken.None);
        if (!result.Success)
        {
            Log(result.Error);
            return;
        }

        Log(result.Value?.Message ?? "终端升级请求已删除。");
        await LoadUpgradeRequestsAsync();
    }

    private async Task DeleteSelectedDealerAsync()
    {
        ApplySettings();
        if (_dealers.SelectedItems.Count == 0 || _dealers.SelectedItems[0].Tag is not DealerRecord dealer)
        {
            Log("请先选择一条经销商记录。");
            return;
        }

        var confirm = MessageBox.Show(this, $"确认禁用经销商 {dealer.DealerNo ?? dealer.Username}？", "确认禁用", MessageBoxButtons.OKCancel, MessageBoxIcon.Warning);
        if (confirm != DialogResult.OK)
        {
            return;
        }

        var result = await _api.DeleteAsync(new DeleteRequest("dealer", dealer.DealerId, _reviewNote.Text.Trim()), CancellationToken.None);
        if (!result.Success)
        {
            Log(result.Error);
            return;
        }

        Log(result.Value?.Message ?? "经销商已禁用。");
        await LoadDealersAsync();
    }

    private void ParseRequestText()
    {
        var parsed = RequestParser.Parse(_requestInput.Text);
        _parsed.Items.Clear();
        AddParsed("来源类型", parsed.Source);
        AddParsed("设备ID", parsed.DeviceId);
        AddParsed("升级请求ID", parsed.UpgradeRequestId);
        AddParsed("经销商ID", parsed.DealerId);
        AddParsed("每日校验码", parsed.DealerDailyCode);
        AddParsed("当前版本", parsed.CurrentVersion);
        AddParsed("目标版本", parsed.TargetVersion);
        AddParsed("目标能力", parsed.TargetCapabilities);
        Log("已解析升级申请内容。");
    }

    private void AddParsed(string name, string? value)
    {
        _parsed.Items.Add(new ListViewItem(new[] { name, string.IsNullOrWhiteSpace(value) ? "-" : value }));
    }

    private async Task RunGuarded(Func<Task> action)
    {
        UseWaitCursor = true;
        try
        {
            await action();
        }
        catch (Exception ex)
        {
            Log("操作失败：" + ex.Message);
        }
        finally
        {
            UseWaitCursor = false;
        }
    }

    private void Log(string message)
    {
        _log.AppendText($"[{DateTime.Now:HH:mm:ss}] {message}{Environment.NewLine}");
    }
}
