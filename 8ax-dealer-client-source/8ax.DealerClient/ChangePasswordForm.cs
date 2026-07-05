namespace EightAxis.DealerClient;

internal sealed class ChangePasswordForm : Form
{
    private readonly TextBox _oldPassword = new() { Dock = DockStyle.Fill, UseSystemPasswordChar = true };
    private readonly TextBox _newPassword = new() { Dock = DockStyle.Fill, UseSystemPasswordChar = true };
    private readonly TextBox _confirmPassword = new() { Dock = DockStyle.Fill, UseSystemPasswordChar = true };

    public string OldPassword => _oldPassword.Text;
    public string NewPassword => _newPassword.Text;

    public ChangePasswordForm()
    {
        Text = "修改密码";
        AutoScaleMode = AutoScaleMode.Dpi;
        Width = 460;
        Height = 260;
        MinimumSize = new Size(420, 240);
        StartPosition = FormStartPosition.CenterParent;
        Font = new Font("Microsoft YaHei UI", 9F, FontStyle.Regular, GraphicsUnit.Point);

        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 2,
            RowCount = 4,
            Padding = new Padding(18, 16, 18, 14),
        };
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 92));
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        Controls.Add(root);

        root.Controls.Add(new Label { Text = "原密码", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, 0);
        root.Controls.Add(_oldPassword, 1, 0);
        root.Controls.Add(new Label { Text = "新密码", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, 1);
        root.Controls.Add(_newPassword, 1, 1);
        root.Controls.Add(new Label { Text = "确认密码", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, 2);
        root.Controls.Add(_confirmPassword, 1, 2);

        var actions = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.RightToLeft,
            WrapContents = false,
            Padding = new Padding(0, 12, 0, 0),
        };
        var ok = new Button { Text = "确认修改", Width = 110, Height = 34, DialogResult = DialogResult.OK };
        var cancel = new Button { Text = "取消", Width = 86, Height = 34, DialogResult = DialogResult.Cancel };
        ok.Click += (_, e) =>
        {
            if (string.IsNullOrWhiteSpace(_oldPassword.Text))
            {
                MessageBox.Show(this, "请填写原密码。", "提示", MessageBoxButtons.OK, MessageBoxIcon.Information);
                DialogResult = DialogResult.None;
                return;
            }
            if (_newPassword.Text.Length < 8)
            {
                MessageBox.Show(this, "新密码至少 8 位。", "提示", MessageBoxButtons.OK, MessageBoxIcon.Information);
                DialogResult = DialogResult.None;
                return;
            }
            if (_newPassword.Text != _confirmPassword.Text)
            {
                MessageBox.Show(this, "两次新密码不一致。", "提示", MessageBoxButtons.OK, MessageBoxIcon.Information);
                DialogResult = DialogResult.None;
                return;
            }
            if (_oldPassword.Text == _newPassword.Text)
            {
                MessageBox.Show(this, "新密码不能和原密码相同。", "提示", MessageBoxButtons.OK, MessageBoxIcon.Information);
                DialogResult = DialogResult.None;
            }
        };
        actions.Controls.Add(ok);
        actions.Controls.Add(cancel);
        root.Controls.Add(actions, 0, 3);
        root.SetColumnSpan(actions, 2);

        AcceptButton = ok;
        CancelButton = cancel;
    }
}
