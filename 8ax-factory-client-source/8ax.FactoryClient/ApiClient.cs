using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text;
using System.Text.Json;

namespace EightAxis.FactoryClient;

internal sealed class ApiClient : IDisposable
{
    private readonly HttpClient _http = new();
    private readonly JsonSerializerOptions _json = new(JsonSerializerDefaults.Web) { WriteIndented = false };

    public string BaseUrl { get; set; } = "https://license.cjwsjzyy.xyz";
    public string AdminUsername { get; set; } = "";
    public string AdminPassword { get; set; } = "";

    public async Task<ApiResult<HealthResponse>> HealthAsync(CancellationToken cancellationToken)
    {
        return await GetAsync<HealthResponse>("/healthz", cancellationToken, auth: false);
    }

    public async Task<ApiResult<DealerListResponse>> GetDealersAsync(CancellationToken cancellationToken)
    {
        return await GetAsync<DealerListResponse>("/api/v1/admin/dealers", cancellationToken, auth: true);
    }

    public async Task<ApiResult<DealerReviewResponse>> ReviewDealerAsync(DealerReviewRequest request, CancellationToken cancellationToken)
    {
        return await PostAsync<DealerReviewRequest, DealerReviewResponse>("/api/v1/admin/dealers/review", request, cancellationToken, auth: true);
    }

    public async Task<ApiResult<DealerUserListResponse>> GetDealerUsersAsync(string? dealerId, CancellationToken cancellationToken)
    {
        var query = string.IsNullOrWhiteSpace(dealerId) ? "" : "?dealerId=" + Uri.EscapeDataString(dealerId);
        return await GetAsync<DealerUserListResponse>("/api/v1/admin/dealer-users" + query, cancellationToken, auth: true);
    }

    public async Task<ApiResult<UpgradeRequestListResponse>> GetUpgradeRequestsAsync(CancellationToken cancellationToken)
    {
        return await GetAsync<UpgradeRequestListResponse>("/api/v1/admin/upgrade-requests", cancellationToken, auth: true);
    }

    public async Task<ApiResult<DeviceListResponse>> GetDevicesAsync(CancellationToken cancellationToken)
    {
        return await GetAsync<DeviceListResponse>("/api/v1/admin/devices", cancellationToken, auth: true);
    }

    public async Task<ApiResult<CommonActionResponse>> ReviewUpgradeRequestAsync(UpgradeRequestReviewRequest request, CancellationToken cancellationToken)
    {
        return await PostAsync<UpgradeRequestReviewRequest, CommonActionResponse>("/api/v1/admin/upgrade-requests/review", request, cancellationToken, auth: true);
    }

    public async Task<ApiResult<CommonActionResponse>> DeleteAsync(DeleteRequest request, CancellationToken cancellationToken)
    {
        return await PostAsync<DeleteRequest, CommonActionResponse>("/api/v1/admin/delete", request, cancellationToken, auth: true);
    }

    public async Task<ApiResult<DeviceAuthorizationUploadResponse>> UploadDeviceAuthorizationAsync(DeviceAuthorizationUploadRequest request, CancellationToken cancellationToken)
    {
        return await PostAsync<DeviceAuthorizationUploadRequest, DeviceAuthorizationUploadResponse>("/api/v1/admin/devices/authorization", request, cancellationToken, auth: true);
    }

    public async Task<ApiResult<OtaPackagePublishResponse>> PublishOtaPackageAsync(OtaPackagePublishRequest request, CancellationToken cancellationToken)
    {
        try
        {
            using var message = new HttpRequestMessage(HttpMethod.Post, BuildUri("/api/v1/admin/ota/packages"));
            AddHeaders(message, auth: true);
            using var content = new MultipartFormDataContent();
            AddFormValue(content, "scope", request.Scope);
            AddFormValue(content, "privateId", request.PrivateId ?? "");
            AddFormValue(content, "privateHash", request.PrivateHash ?? "");
            AddFormValue(content, "product", request.Product);
            AddFormValue(content, "channel", request.Channel);
            AddFormValue(content, "version", request.Version);
            AddFormValue(content, "packageSha256", request.PackageSha256);
            AddFormValue(content, "signatureSha256", request.SignatureSha256);
            AddFormValue(content, "packageSizeBytes", request.PackageSizeBytes.ToString(System.Globalization.CultureInfo.InvariantCulture));
            AddFormValue(content, "signatureSizeBytes", request.SignatureSizeBytes.ToString(System.Globalization.CultureInfo.InvariantCulture));
            AddFormValue(content, "signatureAlg", request.SignatureAlg);
            AddFormValue(content, "keyId", request.KeyId);
            AddFormValue(content, "minCompatibleVersion", request.MinCompatibleVersion);
            AddFormValue(content, "antiRollbackMinVersion", request.AntiRollbackMinVersion);
            AddFormValue(content, "productProfile", request.ProductProfile);
            AddFormValue(content, "hardwareProfile", request.HardwareProfile);
            AddFormValue(content, "reason", request.Reason);
            AddFormValue(content, "scopePolicy", request.ScopePolicy);
            await using var packageStream = File.OpenRead(request.PackagePath);
            await using var signatureStream = File.OpenRead(request.SignaturePath);
            AddFile(content, "package", request.PackagePath, packageStream);
            AddFile(content, "signature", request.SignaturePath, signatureStream);
            message.Content = content;
            using var response = await _http.SendAsync(message, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
            return await ReadResult<OtaPackagePublishResponse>(response, cancellationToken);
        }
        catch (OperationCanceledException)
        {
            return ApiResult<OtaPackagePublishResponse>.Fail("请求已取消。");
        }
        catch (Exception ex)
        {
            return ApiResult<OtaPackagePublishResponse>.Fail(BuildNetworkError(ex));
        }
    }

    public async Task<ApiResult<DriveProfilePublishResponse>> PublishDriveProfileAsync(DriveProfilePublishRequest request, CancellationToken cancellationToken)
    {
        try
        {
            using var message = new HttpRequestMessage(HttpMethod.Post, BuildUri("/api/v1/admin/drive-profiles"));
            AddHeaders(message, auth: true);
            using var content = new MultipartFormDataContent();
            AddFormValue(content, "scope", request.Scope);
            AddFormValue(content, "privateId", request.PrivateId ?? "");
            AddFormValue(content, "privateHash", request.PrivateHash ?? "");
            AddFormValue(content, "profileSha256", request.ProfileSha256);
            AddFormValue(content, "profileSizeBytes", request.ProfileSizeBytes.ToString(System.Globalization.CultureInfo.InvariantCulture));
            AddFormValue(content, "reason", request.Reason);
            await using var profileStream = File.OpenRead(request.ProfilePath);
            AddFile(content, "profile", request.ProfilePath, profileStream);
            message.Content = content;
            using var response = await _http.SendAsync(message, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
            return await ReadResult<DriveProfilePublishResponse>(response, cancellationToken);
        }
        catch (OperationCanceledException)
        {
            return ApiResult<DriveProfilePublishResponse>.Fail("请求已取消。");
        }
        catch (Exception ex)
        {
            return ApiResult<DriveProfilePublishResponse>.Fail(BuildNetworkError(ex));
        }
    }

    private async Task<ApiResult<T>> GetAsync<T>(string path, CancellationToken cancellationToken, bool auth)
    {
        try
        {
            using var message = new HttpRequestMessage(HttpMethod.Get, BuildUri(path));
            AddHeaders(message, auth);
            using var response = await _http.SendAsync(message, cancellationToken);
            return await ReadResult<T>(response, cancellationToken);
        }
        catch (OperationCanceledException)
        {
            return ApiResult<T>.Fail("请求已取消。");
        }
        catch (Exception ex)
        {
            return ApiResult<T>.Fail(BuildNetworkError(ex));
        }
    }

    private async Task<ApiResult<TResponse>> PostAsync<TRequest, TResponse>(string path, TRequest body, CancellationToken cancellationToken, bool auth)
    {
        try
        {
            using var message = new HttpRequestMessage(HttpMethod.Post, BuildUri(path));
            AddHeaders(message, auth);
            message.Content = JsonContent.Create(body, options: _json);
            using var response = await _http.SendAsync(message, cancellationToken);
            return await ReadResult<TResponse>(response, cancellationToken);
        }
        catch (OperationCanceledException)
        {
            return ApiResult<TResponse>.Fail("请求已取消。");
        }
        catch (Exception ex)
        {
            return ApiResult<TResponse>.Fail(BuildNetworkError(ex));
        }
    }

    private Uri BuildUri(string path)
    {
        return new Uri(BaseUrl.TrimEnd('/') + path);
    }

    private static void AddFormValue(MultipartFormDataContent content, string name, string value)
    {
        content.Add(new StringContent(value, Encoding.UTF8), name);
    }

    private static void AddFile(MultipartFormDataContent content, string name, string path, Stream stream)
    {
        var file = new StreamContent(stream);
        file.Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");
        content.Add(file, name, Path.GetFileName(path));
    }

    private string BuildNetworkError(Exception ex)
    {
        var message = ex.Message;
        var localAdminUrl = BaseUrl.Contains("127.0.0.1:18081", StringComparison.OrdinalIgnoreCase)
            || BaseUrl.Contains("localhost:18081", StringComparison.OrdinalIgnoreCase);
        var refused = message.Contains("积极拒绝", StringComparison.OrdinalIgnoreCase)
            || message.Contains("actively refused", StringComparison.OrdinalIgnoreCase)
            || message.Contains("connection refused", StringComparison.OrdinalIgnoreCase)
            || message.Contains("No connection could be made", StringComparison.OrdinalIgnoreCase);
        if (localAdminUrl && refused)
        {
            return "本机 VPS 发布 API 隧道 127.0.0.1:18081 没有监听。请运行 start-vps3-admin-tunnel.ps1，并检查 VPS 的 8ax-ota-admin-api.service。";
        }

        return "网络请求失败：" + message;
    }

    private void AddHeaders(HttpRequestMessage message, bool auth)
    {
        message.Headers.UserAgent.ParseAdd($"8axFactoryClient/{ClientInfo.Version}");
        if (!auth)
        {
            return;
        }

        var raw = $"{AdminUsername}:{AdminPassword}";
        var token = Convert.ToBase64String(Encoding.UTF8.GetBytes(raw));
        message.Headers.Authorization = new AuthenticationHeaderValue("Basic", token);
    }

    private async Task<ApiResult<T>> ReadResult<T>(HttpResponseMessage response, CancellationToken cancellationToken)
    {
        var text = await response.Content.ReadAsStringAsync(cancellationToken);
        if (!response.IsSuccessStatusCode)
        {
            return ApiResult<T>.Fail($"服务端返回 {(int)response.StatusCode}：{TryReadError(text)}");
        }

        if (string.IsNullOrWhiteSpace(text))
        {
            return ApiResult<T>.Fail("服务端返回空内容。");
        }

        try
        {
            var value = JsonSerializer.Deserialize<T>(text, _json);
            return value is null ? ApiResult<T>.Fail("服务端返回内容无法解析。") : ApiResult<T>.Ok(value);
        }
        catch (JsonException ex)
        {
            return ApiResult<T>.Fail("JSON 解析失败：" + ex.Message);
        }
    }

    private static string TryReadError(string text)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            return "无错误内容";
        }

        try
        {
            using var doc = JsonDocument.Parse(text);
            foreach (var name in new[] { "message", "error", "status" })
            {
                if (doc.RootElement.TryGetProperty(name, out var prop))
                {
                    return prop.GetString() ?? text;
                }
            }
        }
        catch (JsonException)
        {
        }

        return text.Length > 300 ? text[..300] + "..." : text;
    }

    public void Dispose()
    {
        _http.Dispose();
    }
}
