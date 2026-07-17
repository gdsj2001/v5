using EightAxis.WinRemote.Protocol;

namespace EightAxis.WinRemote.Rendering;

public sealed class RemoteFrameAssembler
{
    private readonly RemoteFramebuffer _framebuffer;

    public RemoteFrameAssembler(RemoteFramebuffer framebuffer)
    {
        _framebuffer = framebuffer;
    }

    public RemoteFrameApplyResult Apply(RemoteFramePacket packet)
    {
        try
        {
            return packet.Metadata.Type switch
            {
                RemoteMessageTypes.FullFrame => ApplyFullFrame(packet),
                RemoteMessageTypes.DirtyRects => ApplyDirtyRects(packet),
                _ => RemoteFrameApplyResult.Rejected(_framebuffer.FrameId, $"Unsupported frame message type '{packet.Metadata.Type}'."),
            };
        }
        catch (Exception ex) when (ex is ArgumentException or InvalidOperationException or NotSupportedException or OverflowException)
        {
            return RemoteFrameApplyResult.Rejected(_framebuffer.FrameId, ex.Message);
        }
    }

    private RemoteFrameApplyResult ApplyFullFrame(RemoteFramePacket packet)
    {
        FrameMetadata metadata = packet.Metadata;
        ValidateFrameShape(metadata);

        byte[] bgra32 = PixelFormatConverter.ConvertFrameToBgra32(
            metadata.Format,
            metadata.Width,
            metadata.Height,
            metadata.Stride,
            packet.PixelPayload);

        _framebuffer.ApplyFullFrame(bgra32, metadata.FrameId);
        return RemoteFrameApplyResult.AppliedFullFrame(metadata.FrameId);
    }

    private RemoteFrameApplyResult ApplyDirtyRects(RemoteFramePacket packet)
    {
        FrameMetadata metadata = packet.Metadata;
        ValidateFrameShape(metadata);

        if (_framebuffer.FrameId <= 0)
        {
            return RemoteFrameApplyResult.NeedFullFrame(_framebuffer.FrameId, $"Dirty base_frame_id {metadata.BaseFrameId} has no local full frame.");
        }

        if (metadata.BaseFrameId < _framebuffer.FrameId || metadata.FrameId <= _framebuffer.FrameId)
        {
            return RemoteFrameApplyResult.StaleFrame(_framebuffer.FrameId, $"Dirty frame {metadata.FrameId} based on {metadata.BaseFrameId} is older than local frame {_framebuffer.FrameId}.");
        }

        if (metadata.BaseFrameId > _framebuffer.FrameId)
        {
            return RemoteFrameApplyResult.NeedFullFrame(_framebuffer.FrameId, $"Dirty base_frame_id {metadata.BaseFrameId} is ahead of local frame {_framebuffer.FrameId}.");
        }

        if (metadata.FrameId <= metadata.BaseFrameId)
        {
            return RemoteFrameApplyResult.StaleFrame(_framebuffer.FrameId, $"Dirty frame_id {metadata.FrameId} does not advance base frame {metadata.BaseFrameId}.");
        }

        if (metadata.DirtyCount != metadata.Rects.Count)
        {
            return RemoteFrameApplyResult.Rejected(_framebuffer.FrameId, "dirty_count does not match rect count.");
        }

        List<RemoteDirtyRectUpdate> prepared = new(metadata.Rects.Count);
        int payloadOffset = 0;
        foreach (DirtyRectMetadata rect in metadata.Rects)
        {
            if (!String.Equals(rect.Codec, "raw", StringComparison.OrdinalIgnoreCase))
            {
                return RemoteFrameApplyResult.Rejected(_framebuffer.FrameId, $"Unsupported dirty rect codec '{rect.Codec}'.");
            }

            _framebuffer.ValidateRect(rect.X, rect.Y, rect.Width, rect.Height);
            int rectBytes = checked(rect.Width * rect.Height * PixelFormatConverter.BytesPerPixel(metadata.Format));
            if (payloadOffset + rectBytes > packet.PixelPayload.Length)
            {
                return RemoteFrameApplyResult.Rejected(_framebuffer.FrameId, "Dirty rect payload is shorter than metadata requires.");
            }

            byte[] rectBgra32 = PixelFormatConverter.ConvertPackedRectToBgra32(
                metadata.Format,
                rect.Width,
                rect.Height,
                packet.PixelPayload.AsSpan(payloadOffset, rectBytes));

            prepared.Add(new RemoteDirtyRectUpdate(rect.X, rect.Y, rect.Width, rect.Height, rectBgra32));
            payloadOffset += rectBytes;
        }

        if (payloadOffset != packet.PixelPayload.Length)
        {
            return RemoteFrameApplyResult.Rejected(_framebuffer.FrameId, "Dirty rect payload contains trailing bytes.");
        }

        _framebuffer.ApplyDirtyRects(prepared, metadata.FrameId);

        return RemoteFrameApplyResult.AppliedDirtyRects(metadata.FrameId, prepared.Count);
    }

    private void ValidateFrameShape(FrameMetadata metadata)
    {
        if (metadata.Width != _framebuffer.Width || metadata.Height != _framebuffer.Height)
        {
            throw new InvalidOperationException($"Frame dimensions {metadata.Width}x{metadata.Height} do not match framebuffer {_framebuffer.Width}x{_framebuffer.Height}.");
        }
    }
}

public enum RemoteFrameApplyStatus
{
    AppliedFullFrame,
    AppliedDirtyRects,
    NeedFullFrame,
    StaleFrame,
    Rejected,
}

public sealed record RemoteFrameApplyResult(RemoteFrameApplyStatus Status, long FrameId, int DirtyRectCount, string Reason)
{
    public static RemoteFrameApplyResult AppliedFullFrame(long frameId) =>
        new(RemoteFrameApplyStatus.AppliedFullFrame, frameId, 0, String.Empty);

    public static RemoteFrameApplyResult AppliedDirtyRects(long frameId, int dirtyRectCount) =>
        new(RemoteFrameApplyStatus.AppliedDirtyRects, frameId, dirtyRectCount, String.Empty);

    public static RemoteFrameApplyResult NeedFullFrame(long frameId, string reason) =>
        new(RemoteFrameApplyStatus.NeedFullFrame, frameId, 0, reason);

    public static RemoteFrameApplyResult StaleFrame(long frameId, string reason) =>
        new(RemoteFrameApplyStatus.StaleFrame, frameId, 0, reason);

    public static RemoteFrameApplyResult Rejected(long frameId, string reason) =>
        new(RemoteFrameApplyStatus.Rejected, frameId, 0, reason);
}
