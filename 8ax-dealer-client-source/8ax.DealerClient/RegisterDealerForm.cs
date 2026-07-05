namespace EightAxis.DealerClient;

internal sealed class RegisterDealerForm : Form
{
    private readonly TextBox _username = new() { Dock = DockStyle.Fill };
    private readonly TextBox _factoryContactName = new() { Dock = DockStyle.Fill };
    private readonly TextBox _factoryPhone = new() { Dock = DockStyle.Fill };
    private readonly TextBox _factoryWechat = new() { Dock = DockStyle.Fill };
    private readonly TextBox _customerContactName = new() { Dock = DockStyle.Fill };
    private readonly TextBox _customerPhone = new() { Dock = DockStyle.Fill };
    private readonly TextBox _customerWechat = new() { Dock = DockStyle.Fill };
    private readonly TextBox _password = new() { Dock = DockStyle.Fill, UseSystemPasswordChar = true };
    private readonly TextBox _passwordConfirm = new() { Dock = DockStyle.Fill, UseSystemPasswordChar = true };

    public DealerRegisterRequest? Request { get; private set; }

    public RegisterDealerForm(string machineDigest)
    {
        Text = "注册经销商主账号";
        AutoScaleMode = AutoScaleMode.Dpi;
        Width = 900;
        Height = 760;
        MinimumSize = new Size(860, 720);
        StartPosition = FormStartPosition.CenterParent;
        Font = new Font("Microsoft YaHei UI", 9F, FontStyle.Regular, GraphicsUnit.Point);

        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 3,
            Padding = new Padding(16, 14, 16, 12),
        };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 64));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 58));
        Controls.Add(root);

        root.Controls.Add(new Label
        {
            Text = "经销商主账号注册后需要厂家人工审核；厂家联系信息只给厂家联系经销商使用，终端用户联系信息会给终端客户售后联系使用。",
            Dock = DockStyle.Fill,
            TextAlign = ContentAlignment.MiddleLeft,
            Font = new Font(Font.FontFamily, 10F, FontStyle.Bold),
            AutoSize = false,
        }, 0, 0);

        var form = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1 };
        form.RowStyles.Add(new RowStyle(SizeType.Absolute, 52));
        form.RowStyles.Add(new RowStyle(SizeType.Absolute, 146));
        form.RowStyles.Add(new RowStyle(SizeType.Absolute, 146));
        form.RowStyles.Add(new RowStyle(SizeType.Absolute, 104));
        root.Controls.Add(form, 0, 1);

        var account = CreateGrid(2);
        AddField(account, "用户名", _username, 0, 0);
        form.Controls.Add(account, 0, 0);

        var factory = new GroupBox { Text = "厂家联系经销商用", Dock = DockStyle.Fill };
        var factoryGrid = CreateGrid(3);
        AddField(factoryGrid, "联系人", _factoryContactName, 0, 0);
        AddField(factoryGrid, "电话", _factoryPhone, 0, 1);
        AddField(factoryGrid, "微信", _factoryWechat, 0, 2);
        factory.Controls.Add(factoryGrid);
        form.Controls.Add(factory, 0, 1);

        var customer = new GroupBox { Text = "终端用户联系经销商用", Dock = DockStyle.Fill };
        var customerGrid = CreateGrid(3);
        AddField(customerGrid, "联系人", _customerContactName, 0, 0);
        AddField(customerGrid, "电话", _customerPhone, 0, 1);
        AddField(customerGrid, "微信", _customerWechat, 0, 2);
        customer.Controls.Add(customerGrid);
        form.Controls.Add(customer, 0, 2);

        var password = CreateGrid(2);
        AddField(password, "密码", _password, 0, 0);
        AddField(password, "确认密码", _passwordConfirm, 0, 1);
        form.Controls.Add(password, 0, 3);

        var actions = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.RightToLeft,
            WrapContents = false,
            Padding = new Padding(0, 8, 0, 0),
        };
        var submit = new Button { Text = "提交注册", Width = 132, Height = 38, DialogResult = DialogResult.OK };
        var cancel = new Button { Text = "取消", Width = 104, Height = 38, DialogResult = DialogResult.Cancel };
        submit.Click += (_, _) =>
        {
            if (!TryBuildRequest(machineDigest, out var request, out var error))
            {
                MessageBox.Show(this, error, "无法提交", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                DialogResult = DialogResult.None;
                return;
            }

            Request = request;
        };
        actions.Controls.Add(submit);
        actions.Controls.Add(cancel);
        root.Controls.Add(actions, 0, 2);

        AcceptButton = submit;
        CancelButton = cancel;
    }

    private static TableLayoutPanel CreateGrid(int rows)
    {
        var grid = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, Padding = new Padding(12, 7, 12, 6) };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 104));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        for (var i = 0; i < rows; i++)
        {
            grid.RowStyles.Add(new RowStyle(SizeType.Percent, 100F / rows));
        }
        return grid;
    }

    private static void AddField(TableLayoutPanel grid, string label, Control control, int col, int row)
    {
        grid.Controls.Add(new Label { Text = label, Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, col, row);
        grid.Controls.Add(control, col + 1, row);
    }

    private bool TryBuildRequest(string machineDigest, out DealerRegisterRequest? request, out string error)
    {
        request = null;
        error = "";

        if (string.IsNullOrWhiteSpace(_username.Text))
        {
            error = "请填写用户名。";
            return false;
        }

        if (string.IsNullOrWhiteSpace(_factoryContactName.Text) ||
            string.IsNullOrWhiteSpace(_factoryPhone.Text) && string.IsNullOrWhiteSpace(_factoryWechat.Text))
        {
            error = "请填写厂家联系用的联系人，并至少填写电话或微信。";
            return false;
        }

        if (string.IsNullOrWhiteSpace(_customerContactName.Text) ||
            string.IsNullOrWhiteSpace(_customerPhone.Text) && string.IsNullOrWhiteSpace(_customerWechat.Text))
        {
            error = "请填写终端用户联系用的联系人，并至少填写电话或微信。";
            return false;
        }

        if (_password.Text.Length < 8)
        {
            error = "密码至少 8 位。";
            return false;
        }

        if (_password.Text != _passwordConfirm.Text)
        {
            error = "两次输入的密码不一致。";
            return false;
        }

        request = new DealerRegisterRequest(
            _username.Text.Trim(),
            _username.Text.Trim(),
            _factoryContactName.Text.Trim(),
            BlankToDefault(_factoryPhone.Text),
            BlankToNull(_factoryWechat.Text),
            _customerContactName.Text.Trim(),
            BlankToDefault(_customerPhone.Text),
            BlankToNull(_customerWechat.Text),
            "未填写",
            _password.Text,
            "未填写",
            null,
            "dealer-client",
            ClientInfo.Version,
            ClientInfo.Build,
            machineDigest,
            Environment.OSVersion.VersionString);
        return true;
    }

    private static string BlankToDefault(string value)
    {
        var trimmed = value.Trim();
        return trimmed.Length == 0 ? "未填写" : trimmed;
    }

    private static string? BlankToNull(string value)
    {
        var trimmed = value.Trim();
        return trimmed.Length == 0 ? null : trimmed;
    }
}
