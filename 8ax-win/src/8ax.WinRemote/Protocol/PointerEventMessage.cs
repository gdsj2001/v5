using System.Text.Json.Serialization;

namespace EightAxis.WinRemote.Protocol;

public sealed record PointerEventMessage(
    [property: JsonPropertyName("type")] string Type,
    [property: JsonPropertyName("session_id")] string SessionId,
    [property: JsonPropertyName("source")] string Source,
    [property: JsonPropertyName("seq")] long Sequence,
    [property: JsonPropertyName("phase")] string Phase,
    [property: JsonPropertyName("x")] int X,
    [property: JsonPropertyName("y")] int Y,
    [property: JsonPropertyName("button")] string Button,
    [property: JsonPropertyName("client_time_ms")] long ClientTimeMs);

public sealed record ControlRequestMessage(
    [property: JsonPropertyName("type")] string Type,
    [property: JsonPropertyName("session_id")] string SessionId,
    [property: JsonPropertyName("source")] string Source,
    [property: JsonPropertyName("client_time_ms")] long ClientTimeMs);

public sealed record PointerAckMessage(
    [property: JsonPropertyName("type")] string Type,
    [property: JsonPropertyName("session_id")] string SessionId,
    [property: JsonPropertyName("seq")] long Sequence,
    [property: JsonPropertyName("phase")] string Phase,
    [property: JsonPropertyName("accepted")] bool Accepted,
    [property: JsonPropertyName("server_time_ms")] long ServerTimeMs,
    [property: JsonPropertyName("reason")] string? Reason);
