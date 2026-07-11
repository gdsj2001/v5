namespace EightAxis.FactoryClient;

internal sealed partial class MainForm : Form
{
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
