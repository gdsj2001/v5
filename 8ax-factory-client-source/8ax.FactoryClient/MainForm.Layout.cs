namespace EightAxis.FactoryClient;

internal sealed partial class MainForm : Form
{
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
            Dock = DockStyle.Fill,
            AutoEllipsis = true,
            TextAlign = ContentAlignment.MiddleRight,
        };

        var remoteSsh = new Button
        {
            Text = "远程连接",
            Dock = DockStyle.Fill,
            MinimumSize = new Size(0, 34),
        };
        remoteSsh.Click += async (_, _) => await RunGuarded(ConnectSelectedDeviceSshAsync);

        var titleRow = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3 };
        titleRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        // Keep the original 300 px title-right budget so the header does not
        // push the existing form/action buttons beyond the window edge.
        titleRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 112));
        titleRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 188));
        titleRow.Controls.Add(title, 0, 0);
        titleRow.Controls.Add(remoteSsh, 1, 0);
        titleRow.Controls.Add(version, 2, 0);
        panel.Controls.Add(titleRow, 0, 0);

        _serverUrl = new TextBox { Dock = DockStyle.Fill };
        _adminUser = new TextBox { Dock = DockStyle.Fill };
        _adminPassword = new TextBox { Dock = DockStyle.Fill, UseSystemPasswordChar = true };
        _factoryAuthPrivateKey = new TextBox { Dock = DockStyle.Fill };

        var form = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 8, RowCount = 3 };
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
        form.Controls.Add(new Label { Text = "授权私钥", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, 2);
        form.Controls.Add(_factoryAuthPrivateKey, 1, 2);
        form.SetColumnSpan(_factoryAuthPrivateKey, 5);
        var chooseKey = new Button { Text = "选择私钥", Dock = DockStyle.Fill, MinimumSize = new Size(0, 32) };
        chooseKey.Click += (_, _) => ChooseFactoryAuthPrivateKey();
        var resetKey = new Button { Text = "默认路径", Dock = DockStyle.Fill, MinimumSize = new Size(0, 32) };
        resetKey.Click += (_, _) => _factoryAuthPrivateKey.Text = LocalSettings.DefaultFactoryDeviceAuthPrivateKeyPath;
        form.Controls.Add(chooseKey, 6, 2);
        form.Controls.Add(resetKey, 7, 2);
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

}
