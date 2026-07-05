using System.Text.Json.Serialization;

namespace EightAxis.WinRemote.Protocol;

public sealed record DirtyRectMetadata(
    [property: JsonPropertyName("x")] int X,
    [property: JsonPropertyName("y")] int Y,
    [property: JsonPropertyName("w")] int Width,
    [property: JsonPropertyName("h")] int Height,
    [property: JsonPropertyName("codec")] string Codec);
