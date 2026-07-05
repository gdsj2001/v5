using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;

namespace EightAxis.DealerClient;

internal sealed class ApiClient : IDisposable
{
    private readonly HttpClient _http = new();
    private readonly JsonSerializerOptions _json = new(JsonSerializerDefaults.Web)
    {
        WriteIndented = false
    };

    public string BaseUrl { get; set; } = "https://license.cjwsjzyy.xyz";
    public string? SessionToken { get; set; }

    public async Task<ApiResult<HealthResponse>> HealthAsync(CancellationToken cancellationToken)
    {
        return await GetAsync<HealthResponse>("/healthz", cancellationToken);
    }

    public async Task<ApiResult<UpdateCheckResponse>> CheckUpdateAsync(UpdateCheckRequest request, CancellationToken cancellationToken)
    {
        return await PostAsync<UpdateCheckRequest, UpdateCheckResponse>("/api/v1/dealer-client/update-check", request, cancellationToken);
    }

    public async Task<ApiResult<LoginResponse>> LoginAsync(LoginRequest request, CancellationToken cancellationToken)
    {
        return await PostAsync<LoginRequest, LoginResponse>("/api/v1/dealer-user/login", request, cancellationToken);
    }

    public async Task<ApiResult<ChangePasswordResponse>> ChangePasswordAsync(ChangePasswordRequest request, CancellationToken cancellationToken)
    {
        return await PostAsync<ChangePasswordRequest, ChangePasswordResponse>("/api/v1/dealer-user/change-password", request, cancellationToken);
    }

    public async Task<ApiResult<DealerRegisterResponse>> RegisterDealerAsync(DealerRegisterRequest request, CancellationToken cancellationToken)
    {
        return await PostAsync<DealerRegisterRequest, DealerRegisterResponse>("/api/v1/dealer/register", request, cancellationToken);
    }

    public async Task<ApiResult<DailyCodeResponse>> GetDailyCodeAsync(DailyCodeRequest request, CancellationToken cancellationToken)
    {
        return await PostAsync<DailyCodeRequest, DailyCodeResponse>("/api/v1/dealer/daily-code", request, cancellationToken);
    }

    private async Task<ApiResult<T>> GetAsync<T>(string path, CancellationToken cancellationToken)
    {
        try
        {
            using var message = new HttpRequestMessage(HttpMethod.Get, BuildUri(path));
            AddAuth(message);
            using var response = await _http.SendAsync(message, cancellationToken);
            return await ReadResult<T>(response, cancellationToken);
        }
        catch (OperationCanceledException)
        {
            return ApiResult<T>.Fail("请求已取消。");
        }
        catch (Exception ex)
        {
            return ApiResult<T>.Fail("网络请求失败：" + ex.Message);
        }
    }

    private async Task<ApiResult<TResponse>> PostAsync<TRequest, TResponse>(string path, TRequest body, CancellationToken cancellationToken)
    {
        try
        {
            using var message = new HttpRequestMessage(HttpMethod.Post, BuildUri(path));
            AddAuth(message);
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
            return ApiResult<TResponse>.Fail("网络请求失败：" + ex.Message);
        }
    }

    private Uri BuildUri(string path)
    {
        var baseUri = BaseUrl.TrimEnd('/');
        return new Uri(baseUri + path);
    }

    private void AddAuth(HttpRequestMessage message)
    {
        message.Headers.UserAgent.ParseAdd("8axDealerClient/0.1.8");
        if (!string.IsNullOrWhiteSpace(SessionToken))
        {
            message.Headers.Authorization = new AuthenticationHeaderValue("Bearer", SessionToken);
        }
    }

    private async Task<ApiResult<T>> ReadResult<T>(HttpResponseMessage response, CancellationToken cancellationToken)
    {
        var text = await response.Content.ReadAsStringAsync(cancellationToken);
        if (response.StatusCode == HttpStatusCode.NotImplemented)
        {
            return ApiResult<T>.Fail("服务端接口还未上线：" + response.RequestMessage?.RequestUri);
        }

        if (!response.IsSuccessStatusCode)
        {
            var message = TryReadError(text);
            return ApiResult<T>.Fail($"服务端返回 {(int)response.StatusCode}：{message}");
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
            if (doc.RootElement.TryGetProperty("message", out var message))
            {
                return message.GetString() ?? text;
            }
            if (doc.RootElement.TryGetProperty("error", out var error))
            {
                return error.GetString() ?? text;
            }
            if (doc.RootElement.TryGetProperty("status", out var status))
            {
                return status.GetString() ?? text;
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

internal sealed record ApiResult<T>(bool Success, T? Value, string Error)
{
    public static ApiResult<T> Ok(T value) => new(true, value, "");
    public static ApiResult<T> Fail(string error) => new(false, default, error);
}
