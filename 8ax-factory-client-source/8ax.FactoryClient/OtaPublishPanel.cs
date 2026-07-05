using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace EightAxis.FactoryClient;

internal sealed class OtaPublishPanel : UserControl
{
    private const string ScopePolicy = "dna_private_first_no_public_when_private_present";
    private static readonly Regex SafeSegment = new(@"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$", RegexOptions.Compiled);
    private static readonly Regex Sha256Hex = new(@"^[0-9a-fA-F]{64}$", RegexOptions.Compiled);
    private static readonly Regex VpsId = new(@"^[0-9]{6}$", RegexOptions.Compiled);

    private readonly Func<ApiClient> _apiProvider;
    private readonly Action<string> _log;
    private readonly ComboBox _scope = new() { Dock = DockStyle.Fill, DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly TextBox _privateId = new() { Dock = DockStyle.Fill, PlaceholderText = "private 发布必填：6位 VPS分发ID" };
    private readonly TextBox _privateHash = new() { Dock = DockStyle.Fill, PlaceholderText = "private 发布必填：64位 PL DNA hash" };
    private readonly TextBox _product = new() { Dock = DockStyle.Fill, Text = "8ax-v3" };
    private readonly TextBox _channel = new() { Dock = DockStyle.Fill, Text = "stable" };
    private readonly TextBox _version = new() { Dock = DockStyle.Fill };
    private readonly TextBox _packagePath = new() { Dock = DockStyle.Fill, ReadOnly = true };
    private readonly TextBox _signaturePath = new() { Dock = DockStyle.Fill, ReadOnly = true };
    private readonly TextBox _signatureAlg = new() { Dock = DockStyle.Fill, Text = "ed25519" };
    private readonly TextBox _keyId = new() { Dock = DockStyle.Fill };
    private readonly TextBox _minCompatibleVersion = new() { Dock = DockStyle.Fill };
    private readonly TextBox _antiRollbackMinVersion = new() { Dock = DockStyle.Fill };
    private readonly TextBox _productProfile = new() { Dock = DockStyle.Fill, Text = "bus5" };
    private readonly TextBox _hardwareProfile = new() { Dock = DockStyle.Fill };
    private readonly TextBox _reason = new() { Dock = DockStyle.Fill, Multiline = true, ScrollBars = ScrollBars.Vertical };
    private readonly TextBox _output = new() { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical };
    private readonly Button _publish = new() { Text = "发布OTA包", Dock = DockStyle.Fill };
    private readonly Button _cancel = new() { Text = "取消发布", Dock = DockStyle.Fill, Enabled = false };
    private CancellationTokenSource? _publishCts;

    public OtaPublishPanel(Func<ApiClient> apiProvider, Action<string> log)
    {
        _apiProvider = apiProvider;
        _log = log;
        Dock = DockStyle.Fill;
        BuildUi();
    }

    private void BuildUi()
    {
        _scope.Items.AddRange(new object[] { "public", "private" });
        _scope.SelectedIndex = 0;
        _scope.SelectedIndexChanged += (_, _) => SetPrivateInputsEnabled();
        _privateId.Enabled = false;
        _privateHash.Enabled = false;

        var root = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 2, ColumnCount = 1, Padding = new Padding(10) };
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 62));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 38));
        Controls.Add(root);

        var form = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 4, RowCount = 12 };
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 130));
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 150));
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        root.Controls.Add(form, 0, 0);

        AddRow(form, 0, "发布范围", _scope, "私有ID", _privateId);
        AddRow(form, 1, "DNA private hash", _privateHash, "产品", _product);
        AddRow(form, 2, "通道", _channel, "版本", _version);
        AddRow(form, 3, "签名算法", _signatureAlg, "key id", _keyId);
        AddRow(form, 4, "最低兼容版本", _minCompatibleVersion, "防降级最低版本", _antiRollbackMinVersion);
        AddRow(form, 5, "产品profile", _productProfile, "硬件profile", _hardwareProfile);
        AddRow(form, 6, "选择策略", new TextBox { Dock = DockStyle.Fill, ReadOnly = true, Text = ScopePolicy }, "", new Label());
        AddFileRow(form, 7, "OTA包", _packagePath, "选择OTA包", () => ChooseFile(_packagePath));
        AddFileRow(form, 8, "签名文件", _signaturePath, "选择签名", () => ChooseFile(_signaturePath));
        form.Controls.Add(new Label { Text = "发布原因", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, 9);
        form.Controls.Add(_reason, 1, 9);
        form.SetColumnSpan(_reason, 3);
        var actions = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3 };
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 120));
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 120));
        actions.Controls.Add(new Label { Text = "发布到 VPS 后，板端仍需走 OTA client 下载/验签/确认/回滚状态机。", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, 0);
        actions.Controls.Add(_publish, 1, 0);
        actions.Controls.Add(_cancel, 2, 0);
        form.Controls.Add(actions, 0, 10);
        form.SetColumnSpan(actions, 4);
        _publish.Click += async (_, _) => await PublishAsync();
        _cancel.Click += (_, _) => _publishCts?.Cancel();

        var resultGroup = new GroupBox { Text = "发布结果 / manifest 摘要", Dock = DockStyle.Fill };
        resultGroup.Controls.Add(_output);
        root.Controls.Add(resultGroup, 0, 1);
    }

    private bool IsPrivateScope => string.Equals(_scope.SelectedItem?.ToString(), "private", StringComparison.OrdinalIgnoreCase);

    private void SetPrivateInputsEnabled()
    {
        _privateId.Enabled = IsPrivateScope;
        _privateHash.Enabled = IsPrivateScope;
    }

    private static void AddRow(TableLayoutPanel form, int row, string leftLabel, Control left, string rightLabel, Control right)
    {
        form.RowStyles.Add(new RowStyle(SizeType.Absolute, 36));
        form.Controls.Add(new Label { Text = leftLabel, Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, row);
        form.Controls.Add(left, 1, row);
        form.Controls.Add(new Label { Text = rightLabel, Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 2, row);
        form.Controls.Add(right, 3, row);
    }

    private static void AddFileRow(TableLayoutPanel form, int row, string label, TextBox path, string buttonText, Action choose)
    {
        form.RowStyles.Add(new RowStyle(SizeType.Absolute, 36));
        form.Controls.Add(new Label { Text = label, Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft }, 0, row);
        form.Controls.Add(path, 1, row);
        form.SetColumnSpan(path, 2);
        var button = new Button { Text = buttonText, Dock = DockStyle.Fill };
        button.Click += (_, _) => choose();
        form.Controls.Add(button, 3, row);
    }

    private void ChooseFile(TextBox target)
    {
        using var dialog = new OpenFileDialog
        {
            Title = "选择OTA发布文件",
            CheckFileExists = true,
            Multiselect = false,
            Filter = "OTA files (*.ota;*.zip;*.tar;*.tar.gz;*.sig)|*.ota;*.zip;*.tar;*.gz;*.sig|All files (*.*)|*.*",
        };
        if (dialog.ShowDialog(this) == DialogResult.OK)
        {
            target.Text = dialog.FileName;
        }
    }

    private async Task PublishAsync()
    {
        _publish.Enabled = false;
        _cancel.Enabled = true;
        _publishCts = new CancellationTokenSource();
        try
        {
            OtaPackagePublishRequest request = await BuildRequestAsync(_publishCts.Token);
            _output.Text = BuildPreview(request) + "\r\n正在上传 VPS...";
            var result = await _apiProvider().PublishOtaPackageAsync(request, _publishCts.Token);
            if (!result.Success)
            {
                _output.Text = BuildPreview(request) + "\r\n\r\n发布失败：\r\n" + result.Error;
                _log("OTA 发布失败：" + result.Error);
                MessageBox.Show(this, result.Error, "OTA发布失败", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            _output.Text = BuildPreview(request) + "\r\n\r\nVPS返回：\r\n" + BuildResponse(result.Value);
            _log(result.Value?.Message ?? "OTA 包已上传 VPS。");
            MessageBox.Show(this, result.Value?.Message ?? "OTA 包已上传 VPS。", "OTA发布完成", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        catch (OperationCanceledException)
        {
            _output.AppendText("\r\n发布已取消。");
            _log("OTA 发布已取消。");
        }
        catch (Exception ex)
        {
            _output.AppendText("\r\n发布失败：" + ex.Message);
            _log("OTA 发布失败：" + ex.Message);
            MessageBox.Show(this, ex.Message, "OTA发布失败", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
        finally
        {
            _publishCts.Dispose();
            _publishCts = null;
            _cancel.Enabled = false;
            _publish.Enabled = true;
        }
    }

    private async Task<OtaPackagePublishRequest> BuildRequestAsync(CancellationToken cancellationToken)
    {
        string scope = _scope.SelectedItem?.ToString() ?? "public";
        string privateId = _privateId.Text.Trim();
        string privateHash = _privateHash.Text.Trim().ToLowerInvariant();
        if (scope == "private" && !VpsId.Match(privateId).Success)
        {
            throw new InvalidOperationException("private 发布必须填写 6 位 VPS分发ID。");
        }
        if (scope == "private" && !Sha256Hex.Match(privateHash).Success)
        {
            throw new InvalidOperationException("private 发布必须填写 64 位 PL DNA hash。");
        }

        string packagePath = RequireFile(_packagePath.Text, "OTA包");
        string signaturePath = RequireFile(_signaturePath.Text, "签名文件");
        string product = RequireSegment(_product.Text, "产品");
        string channel = RequireSegment(_channel.Text, "通道");
        string version = RequireSegment(_version.Text, "版本");
        string signatureAlg = RequireSegment(_signatureAlg.Text, "签名算法");
        string keyId = RequireSegment(_keyId.Text, "key id");
        string minCompatible = RequireSegment(_minCompatibleVersion.Text, "最低兼容版本");
        string antiRollback = RequireSegment(_antiRollbackMinVersion.Text, "防降级最低版本");
        string productProfile = RequireSegment(_productProfile.Text, "产品profile");
        string hardwareProfile = RequireSegment(_hardwareProfile.Text, "硬件profile");
        string reason = _reason.Text.Trim();
        if (string.IsNullOrWhiteSpace(reason))
        {
            throw new InvalidOperationException("发布原因必填。");
        }

        FileInfo packageInfo = new(packagePath);
        FileInfo signatureInfo = new(signaturePath);
        string packageSha = await ComputeSha256Async(packagePath, cancellationToken);
        string signatureSha = await ComputeSha256Async(signaturePath, cancellationToken);
        return new OtaPackagePublishRequest(
            scope,
            scope == "private" ? privateId : null,
            scope == "private" ? privateHash : null,
            product,
            channel,
            version,
            packagePath,
            signaturePath,
            packageSha,
            signatureSha,
            packageInfo.Length,
            signatureInfo.Length,
            signatureAlg,
            keyId,
            minCompatible,
            antiRollback,
            productProfile,
            hardwareProfile,
            reason,
            ScopePolicy);
    }

    private static string RequireFile(string value, string label)
    {
        string path = value.Trim();
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
        {
            throw new InvalidOperationException($"{label}不存在。");
        }
        return path;
    }

    private static string RequireSegment(string value, string label)
    {
        string text = value.Trim();
        if (!SafeSegment.Match(text).Success)
        {
            throw new InvalidOperationException($"{label}只能使用字母、数字、点、下划线和横线，并且不能为空。");
        }
        return text;
    }

    private static async Task<string> ComputeSha256Async(string path, CancellationToken cancellationToken)
    {
        await using FileStream stream = new(path, FileMode.Open, FileAccess.Read, FileShare.Read, 1024 * 1024, FileOptions.Asynchronous | FileOptions.SequentialScan);
        byte[] hash = await SHA256.HashDataAsync(stream, cancellationToken);
        return Convert.ToHexString(hash).ToLowerInvariant();
    }

    private static string BuildPreview(OtaPackagePublishRequest request)
    {
        string target = request.Scope == "private"
            ? $"private/{request.PrivateId}/ota/{request.Product}/{request.Channel}"
            : $"public/{request.Product}/{request.Channel}";
        return
            $"scope: {request.Scope}\r\n" +
            $"target: {target}\r\n" +
            $"version: {request.Version}\r\n" +
            $"package: {Path.GetFileName(request.PackagePath)}\r\n" +
            $"package_sha256: {request.PackageSha256}\r\n" +
            $"signature: {Path.GetFileName(request.SignaturePath)}\r\n" +
            $"signature_sha256: {request.SignatureSha256}\r\n" +
            $"scope_policy: {request.ScopePolicy}\r\n" +
            "operator_confirmation_required: true";
    }

    private static string BuildResponse(OtaPackagePublishResponse? response)
    {
        if (response is null)
        {
            return "服务端未返回发布详情。";
        }

        return
            $"success: {response.Success}\r\n" +
            $"message: {response.Message ?? "-"}\r\n" +
            $"source_scope: {response.SourceScope ?? "-"}\r\n" +
            $"target_rel: {response.TargetRel ?? "-"}\r\n" +
            $"vps_distribution_id: {response.VpsDistributionId ?? "-"}\r\n" +
            $"dna_binding: {response.DnaBinding ?? "-"}\r\n" +
            $"private_folder: {response.PrivateFolder ?? "-"}\r\n" +
            $"manifest_sha256: {response.ManifestSha256 ?? "-"}\r\n" +
            $"package_sha256: {response.PackageSha256 ?? "-"}\r\n" +
            $"signature_sha256: {response.SignatureSha256 ?? "-"}";
    }
}
