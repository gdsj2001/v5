namespace EightAxis.DealerClient;

internal static class ClientInfo
{
    public const string Version = "0.1.15";
    public const string Build = "2026.04.29.010";
    public const string Channel = "stable";
}

internal sealed record HealthResponse(
    string? Status,
    string? Service,
    string? Version,
    string? Host,
    string? Time);

internal sealed record UpdateCheckRequest(
    string Version,
    string Build,
    string Channel,
    string WindowsVersion,
    string MachineDigest);

internal sealed record UpdateCheckResponse(
    bool AllowContinue,
    string? LatestVersion,
    bool ForceUpdate,
    string? Status,
    string? Message,
    string? ManifestUrl,
    string? DownloadUrl,
    string? Sha256,
    string? Signature);

internal sealed record LoginRequest(
    string Username,
    string Password,
    string MachineDigest,
    string ClientVersion,
    string ClientBuild,
    string Channel,
    string WindowsVersion);

internal sealed record LoginResponse(
    string? SessionToken,
    string? DealerId,
    string? DealerName,
    string? Username,
    string? AccountType,
    string? Role,
    bool CanShowDailyCode,
    bool CanHandleRequests,
    string? InviteRegisterUrl,
    DateTimeOffset? ExpiresAt,
    string? Message);

internal sealed record ChangePasswordRequest(
    string OldPassword,
    string NewPassword,
    string MachineDigest,
    string ClientVersion,
    string ClientBuild);

internal sealed record ChangePasswordResponse(
    bool Success,
    string? Message);

internal sealed record DealerRegisterRequest(
    string Username,
    string DealerName,
    string ContactName,
    string Phone,
    string? Wechat,
    string CustomerContactName,
    string CustomerPhone,
    string? CustomerWechat,
    string Region,
    string Password,
    string ServiceScope,
    string? Qualification,
    string Source,
    string ClientVersion,
    string ClientBuild,
    string MachineDigest,
    string WindowsVersion);

internal sealed record DealerRegisterResponse(
    string? DealerId,
    string? ReviewStatus,
    string? Message);

internal sealed record DailyCodeRequest(
    string? DeviceId,
    string? UpgradeRequestId,
    string? TargetSummary);

internal sealed record DailyCodeResponse(
    string? DealerDailyCode,
    DateTimeOffset? ExpiresAt,
    string? Scope,
    int? RemainingUses,
    string? Message);
