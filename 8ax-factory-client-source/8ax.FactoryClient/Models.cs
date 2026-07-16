namespace EightAxis.FactoryClient;

internal static class ClientInfo
{
    public const string Version = "0.1.15";
    public const string Build = "2026.07.14.003";
}

internal sealed record HealthResponse(
    string? Status,
    string? Service,
    string? Version,
    string? Host,
    string? Time);

internal sealed record DealerListResponse(
    bool Success,
    List<DealerRecord> Dealers,
    string? Message);

internal sealed record DealerRecord(
    string DealerId,
    string? DealerNo,
    string Username,
    string? DealerName,
    string? ReviewStatus,
    string? CooperationStatus,
    string? ContactName,
    string? Phone,
    string? Wechat,
    string? CustomerContactName,
    string? CustomerPhone,
    string? CustomerWechat,
    string? ReviewNote,
    DateTimeOffset? CreatedAt);

internal sealed record DealerReviewRequest(
    string DealerId,
    string Decision,
    string? Note);

internal sealed record DealerReviewResponse(
    bool Success,
    string? DealerId,
    string? Username,
    string? ReviewStatus,
    string? CooperationStatus,
    string? Message,
    string? Error);

internal sealed record DealerUserListResponse(
    bool Success,
    List<DealerUserRecord> Users,
    string? Message);

internal sealed record DealerUserRecord(
    string DealerUserId,
    string? DealerUserNo,
    string DealerId,
    string Username,
    string? DisplayName,
    string? Phone,
    string? Role,
    string? Status,
    bool CanHandleRequests,
    bool CanShowDailyCode,
    DateTimeOffset? LastLoginAt,
    DateTimeOffset? CreatedAt);

internal sealed record UpgradeRequestListResponse(
    bool Success,
    List<UpgradeRequestRecord> Requests,
    string? Message);

internal sealed record UpgradeRequestRecord(
    string UpgradeRequestId,
    string? UpgradeRequestNo,
    string? DeviceId,
    string? DealerId,
    string? DealerUsername,
    string? DealerName,
    string? DealerUserId,
    string? EmployeeUsername,
    string? EmployeeName,
    string? Status,
    object? RequestPayload,
    string? QrDigest,
    DateTimeOffset? CreatedAt,
    DateTimeOffset? ExpiresAt);

internal sealed record UpgradeRequestReviewRequest(
    string UpgradeRequestId,
    string Decision,
    string? Note);

internal sealed record DeviceListResponse(
    bool Success,
    List<DeviceRecord> Devices,
    string? Message);

internal sealed record DeviceRecord(
    string DeviceId,
    string? VpsDistributionId,
    string? PlDeviceDna,
    string? PlDnaHash,
    string? DeviceIdSource,
    string? ActivationStatus,
    string? AuthorizationStatus,
    string? InitialVersion,
    string? CurrentVersion,
    string? DevicePublicKeySha256,
    DateTimeOffset? FactoryRegisteredAt,
    DateTimeOffset? CreatedAt,
    DateTimeOffset? UpdatedAt,
    List<DeviceIpAccessRecord>? IpAccessRecords,
    string? LatestIpAccess);

internal sealed record DeviceIpAccessRecord(
    string? Nonce,
    DateTimeOffset? Time,
    string? Ip,
    string? Status,
    string? Path);

internal sealed record RemoteSshStatusResponse(
    bool Success,
    string DeviceId,
    bool Registered,
    bool Online,
    int? AssignedPort,
    string? VpsHost,
    int? VpsPort,
    string? TunnelUser,
    DateTimeOffset? UpdatedAt,
    string? Message);

internal sealed record DeleteRequest(
    string TargetType,
    string TargetId,
    string? Note);

internal sealed record CommonActionResponse(
    bool Success,
    string? Message,
    string? TargetId,
    string? Name,
    bool? AuthorizationDeleted);

internal sealed record DeviceAuthorizationUploadRequest(
    string DeviceId,
    string PlDnaHash,
    object DeviceAuthorization);

internal sealed record DeviceAuthorizationUploadResponse(
    bool Success,
    string? Message,
    string? DeviceId,
    string? PlDnaHash,
    string? SignatureHash);

internal sealed record OtaPackagePublishRequest(
    string Scope,
    string? PrivateId,
    string? PrivateHash,
    string Product,
    string Channel,
    string Version,
    string PackagePath,
    string SignaturePath,
    string PackageSha256,
    string SignatureSha256,
    long PackageSizeBytes,
    long SignatureSizeBytes,
    string SignatureAlg,
    string KeyId,
    string MinCompatibleVersion,
    string AntiRollbackMinVersion,
    string ProductProfile,
    string HardwareProfile,
    string Reason,
    string ScopePolicy);

internal sealed record DriveProfilePublishRequest(
    string Scope,
    string? PrivateId,
    string? PrivateHash,
    string ProfilePath,
    string ProfileSha256,
    long ProfileSizeBytes,
    string Reason);

internal sealed record DriveProfilePublishResponse(
    bool Success,
    string? Message,
    string? SourceScope,
    string? TargetRel,
    string? VpsDistributionId,
    string? DnaBinding,
    string? PlDnaHash,
    string? ProfileSha256,
    long? ProfileSizeBytes);

internal sealed record OtaPackagePublishResponse(
    bool Success,
    string? Message,
    string? SourceScope,
    string? TargetRel,
    string? VpsDistributionId,
    string? DnaBinding,
    string? PrivateFolder,
    string? ManifestSha256,
    string? PackageSha256,
    string? SignatureSha256);

internal sealed record ParsedRequest(
    string Source,
    string? DeviceId,
    string? UpgradeRequestId,
    string? DealerId,
    string? DealerDailyCode,
    string? CurrentVersion,
    string? TargetVersion,
    string? TargetCapabilities,
    string RawText);
