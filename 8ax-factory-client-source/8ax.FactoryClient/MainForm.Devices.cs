namespace EightAxis.FactoryClient;

internal sealed partial class MainForm : Form
{
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

        var privateKeyPath = _settings.FactoryDeviceAuthPrivateKeyPath;
        if (!File.Exists(privateKeyPath))
        {
            var message =
                $"缺少工厂授权私钥文件：{privateKeyPath}\n\n" +
                "请点击顶部“选择私钥”，选择本机 factory-device-auth-private.pem 后重试。";
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

}
