using System.Text.Json.Serialization;

namespace EightAxis.WinRemote.Protocol;

public sealed record FrameMetadata(
    [property: JsonPropertyName("type")] string Type,
    [property: JsonPropertyName("frame_id")] long FrameId,
    [property: JsonPropertyName("base_frame_id")] long BaseFrameId,
    [property: JsonPropertyName("monotonic_ns")] long MonotonicNs,
    [property: JsonPropertyName("width")] int Width,
    [property: JsonPropertyName("height")] int Height,
    [property: JsonPropertyName("stride")] int Stride,
    [property: JsonPropertyName("format")] string Format,
    [property: JsonPropertyName("dirty_count")] int DirtyCount,
    [property: JsonPropertyName("rects")] IReadOnlyList<DirtyRectMetadata> Rects)
{
    public static FrameMetadata FullFrame(long frameId, int width, int height, int stride, string format) =>
        new(
            RemoteMessageTypes.FullFrame,
            frameId,
            0,
            Environment.TickCount64 * 1_000_000L,
            width,
            height,
            stride,
            format,
            0,
            Array.Empty<DirtyRectMetadata>());

    public static FrameMetadata DirtyRects(
        long frameId,
        long baseFrameId,
        int width,
        int height,
        int stride,
        string format,
        IReadOnlyList<DirtyRectMetadata> rects) =>
        new(
            RemoteMessageTypes.DirtyRects,
            frameId,
            baseFrameId,
            Environment.TickCount64 * 1_000_000L,
            width,
            height,
            stride,
            format,
            rects.Count,
            rects);
}
