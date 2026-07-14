using System.Diagnostics;

namespace EightAxis.FactoryClient;

internal sealed partial class MainForm : Form
{
    private async Task ConnectSelectedDeviceSshAsync()
    {
        ApplySettings();
        if (_devices.CurrentRow?.Tag is not DeviceRecord device)
        {
            ShowRemoteSshMessage("未选择设备", "请先在“设备DNA”表中选择一台设备。", MessageBoxIcon.Warning);
            return;
        }

        var deviceId = (device.VpsDistributionId ?? device.DeviceId ?? "").Trim();
        if (deviceId.Length != 6 || !deviceId.All(char.IsDigit))
        {
            ShowRemoteSshMessage("设备身份无效", "选中设备没有合法的 6 位 VPS 分发 ID。", MessageBoxIcon.Error);
            return;
        }
        if (!IsDeviceAuthorized(device))
        {
            ShowRemoteSshMessage("设备未授权", "请先为选中设备生成并下发包含远程 SSH 权限的授权文件。", MessageBoxIcon.Warning);
            return;
        }

        var result = await _api.GetRemoteSshStatusAsync(deviceId, CancellationToken.None);
        if (!result.Success || result.Value is null)
        {
            var error = string.IsNullOrWhiteSpace(result.Error) ? "VPS 未返回远程 SSH 状态。" : result.Error;
            Log($"远程 SSH 状态读取失败：设备 {deviceId} / {error}");
            ShowRemoteSshMessage("远程连接失败", error, MessageBoxIcon.Error);
            return;
        }

        var status = result.Value;
        if (!string.Equals(status.DeviceId, deviceId, StringComparison.Ordinal) ||
            status.AssignedPort is null ||
            status.AssignedPort.Value < 25000 ||
            status.AssignedPort.Value > 44999)
        {
            ShowRemoteSshMessage("远程连接失败", "VPS 返回的设备身份或端口无效。", MessageBoxIcon.Error);
            return;
        }
        if (!status.Registered || !status.Online)
        {
            var message = string.IsNullOrWhiteSpace(status.Message)
                ? "设备反向 SSH 通道尚未在线，请确认设备联网并已下载最新授权。"
                : status.Message;
            Log($"远程 SSH 未在线：设备 {deviceId}");
            ShowRemoteSshMessage("设备不在线", message, MessageBoxIcon.Information);
            return;
        }

        var sshPath = Path.Combine(Environment.SystemDirectory, "OpenSSH", "ssh.exe");
        if (!File.Exists(sshPath))
        {
            ShowRemoteSshMessage("缺少 SSH 客户端", $"Windows 系统 SSH 不存在：{sshPath}", MessageBoxIcon.Error);
            return;
        }

        var port = status.AssignedPort.Value;
        var hostKeyAlias = "8ax-device-" + deviceId;
        var script =
            "& '" + sshPath.Replace("'", "''") + "' " +
            "-o 'HostKeyAlias=" + hostKeyAlias + "' " +
            "-o 'StrictHostKeyChecking=ask' " +
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
        Log($"已启动远程 SSH：设备 {deviceId} / VPS loopback 端口 {port}。");
    }

    private void ShowRemoteSshMessage(string title, string message, MessageBoxIcon icon)
    {
        MessageBox.Show(this, message, title, MessageBoxButtons.OK, icon);
    }
}
