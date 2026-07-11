using System.Diagnostics;
using System.Security.Cryptography;

namespace EightAxis.DealerClient;

internal sealed partial class MainForm : Form
{
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
