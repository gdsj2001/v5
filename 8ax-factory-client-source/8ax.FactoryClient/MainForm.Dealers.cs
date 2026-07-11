namespace EightAxis.FactoryClient;

internal sealed partial class MainForm : Form
{
    private void LoadSettings()
    {
        _serverUrl.Text = _settings.ServerUrl;
        _adminUser.Text = _settings.AdminUsername;
        _adminPassword.Text = LocalSettings.DefaultAdminPassword;
        _factoryAuthPrivateKey.Text = _settings.FactoryDeviceAuthPrivateKeyPath;
        Log("厂家授权控制台启动。");
    }

    private void ApplySettings()
    {
        _settings.ServerUrl = string.IsNullOrWhiteSpace(_serverUrl.Text) ? LocalSettings.PrimaryServerUrl : _serverUrl.Text.Trim();
        _settings.AdminUsername = _adminUser.Text.Trim();
        _settings.FactoryDeviceAuthPrivateKeyPath = NormalizeFactoryAuthPrivateKeyPath(_factoryAuthPrivateKey.Text);
        _factoryAuthPrivateKey.Text = _settings.FactoryDeviceAuthPrivateKeyPath;
        _settings.Save();

        _api.BaseUrl = _settings.ServerUrl;
        _api.AdminUsername = _settings.AdminUsername;
        _api.AdminPassword = _adminPassword.Text;
    }

    private void ChooseFactoryAuthPrivateKey()
    {
        using var dialog = new OpenFileDialog
        {
            Title = "选择工厂授权私钥",
            Filter = "PEM 私钥 (*.pem)|*.pem|所有文件 (*.*)|*.*",
            CheckFileExists = true,
            Multiselect = false,
        };
        var current = NormalizeFactoryAuthPrivateKeyPath(_factoryAuthPrivateKey.Text);
        var currentDir = Path.GetDirectoryName(current);
        if (!string.IsNullOrWhiteSpace(currentDir) && Directory.Exists(currentDir))
        {
            dialog.InitialDirectory = currentDir;
        }
        if (dialog.ShowDialog(this) == DialogResult.OK)
        {
            _factoryAuthPrivateKey.Text = dialog.FileName;
            ApplySettings();
        }
    }

    private static string NormalizeFactoryAuthPrivateKeyPath(string? value)
    {
        var path = string.IsNullOrWhiteSpace(value)
            ? LocalSettings.DefaultFactoryDeviceAuthPrivateKeyPath
            : Environment.ExpandEnvironmentVariables(value.Trim());
        return Path.GetFullPath(path);
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

}
