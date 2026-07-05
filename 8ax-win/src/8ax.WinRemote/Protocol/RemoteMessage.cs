namespace EightAxis.WinRemote.Protocol;

public static class RemoteMessageTypes
{
    public const string FullFrame = "full_frame";
    public const string DirtyRects = "dirty_rects";
    public const string PointerEvent = "pointer_event";
    public const string PointerAck = "pointer_ack";
    public const string PointerReject = "pointer_reject";
    public const string ControlRequest = "control_request";
    public const string ControlGrant = "control_grant";
    public const string ControlRevoke = "control_revoke";
    public const string Heartbeat = "heartbeat";
    public const string SessionState = "session_state";
}

public static class RemotePixelFormats
{
    public const string Bgra32 = "bgra32";
    public const string Rgb565 = "rgb565";
}

public sealed record RemoteInfoMessage(
    string ProtocolVersion,
    int Width,
    int Height,
    string PixelFormat,
    int Stride,
    bool ViewOnly,
    RemoteSystemMetrics? SystemMetrics = null);

public sealed record RemoteSystemMetrics(
    double? Cpu0Percent,
    double? Cpu1Percent,
    double? MemoryPercent,
    double? DiskPercent,
    long? MemoryUsedBytes = null,
    long? MemoryTotalBytes = null,
    long? DiskUsedBytes = null,
    long? DiskTotalBytes = null);

public sealed record ProgramUploadResult(
    string Schema,
    string FileName,
    string DestinationPath,
    long SizeBytes,
    string Sha256,
    bool Overwrote,
    long UploadedAtUnixMs);

public sealed record ProgramFileInfo(
    string FileName,
    string DestinationPath,
    bool Exists = true,
    long? SizeBytes = null,
    long? ModifiedAtUnixMs = null,
    bool Editable = false,
    string? Sha256 = null);

public sealed record ProgramListResult(
    string Schema,
    string ProgramDir,
    int Count,
    IReadOnlyList<ProgramFileInfo> Files);

public sealed record ProgramFileContentResult(
    string Schema,
    string FileName,
    string DestinationPath,
    long SizeBytes,
    string Sha256,
    long ModifiedAtUnixMs,
    string Text);

public sealed record ProgramDeleteResult(
    string Schema,
    string FileName,
    string DestinationPath,
    bool Deleted,
    long DeletedAtUnixMs,
    ProgramFileInfo? Previous = null);

public sealed record OtaUpgradeRequest(
    string Schema,
    string Source,
    long RequestedAtUnixMs,
    string ScopePolicy);

public sealed record OtaUpgradeResult(
    string Schema,
    string Status,
    string Code,
    string Message,
    string? SelectedScope = null,
    string? JobId = null,
    bool Cancellable = false,
    long? RequestedAtUnixMs = null);

public sealed record RemoteFramePacket(FrameMetadata Metadata, byte[] PixelPayload);
