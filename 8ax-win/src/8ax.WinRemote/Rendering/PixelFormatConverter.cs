using EightAxis.WinRemote.Protocol;

namespace EightAxis.WinRemote.Rendering;

public static class PixelFormatConverter
{
    public static int BytesPerPixel(string pixelFormat) =>
        Normalize(pixelFormat) switch
        {
            RemotePixelFormats.Bgra32 => 4,
            RemotePixelFormats.Rgb565 => 2,
            _ => throw new NotSupportedException($"Unsupported remote pixel format '{pixelFormat}'."),
        };

    public static byte[] ConvertFrameToBgra32(string pixelFormat, int width, int height, int stride, ReadOnlySpan<byte> payload)
    {
        ValidateDimensions(width, height);
        int bytesPerPixel = BytesPerPixel(pixelFormat);
        int rowBytes = checked(width * bytesPerPixel);
        if (stride < rowBytes)
        {
            throw new InvalidOperationException($"Stride {stride} is smaller than row bytes {rowBytes}.");
        }

        int requiredBytes = checked(stride * height);
        if (payload.Length < requiredBytes)
        {
            throw new InvalidOperationException($"Payload is shorter than required frame bytes {requiredBytes}.");
        }

        return Normalize(pixelFormat) switch
        {
            RemotePixelFormats.Bgra32 => CopyBgraRows(width, height, stride, payload),
            RemotePixelFormats.Rgb565 => ConvertRgb565Rows(width, height, stride, payload),
            _ => throw new NotSupportedException($"Unsupported remote pixel format '{pixelFormat}'."),
        };
    }

    public static byte[] ConvertPackedRectToBgra32(string pixelFormat, int width, int height, ReadOnlySpan<byte> payload)
    {
        ValidateDimensions(width, height);
        int stride = checked(width * BytesPerPixel(pixelFormat));
        return ConvertFrameToBgra32(pixelFormat, width, height, stride, payload);
    }

    private static byte[] CopyBgraRows(int width, int height, int stride, ReadOnlySpan<byte> payload)
    {
        int rowBytes = checked(width * 4);
        byte[] output = new byte[checked(rowBytes * height)];
        for (int y = 0; y < height; y++)
        {
            payload.Slice(y * stride, rowBytes).CopyTo(output.AsSpan(y * rowBytes, rowBytes));
        }

        return output;
    }

    private static byte[] ConvertRgb565Rows(int width, int height, int stride, ReadOnlySpan<byte> payload)
    {
        byte[] output = new byte[checked(width * height * 4)];
        int outIndex = 0;
        for (int y = 0; y < height; y++)
        {
            ReadOnlySpan<byte> row = payload.Slice(y * stride, width * 2);
            for (int x = 0; x < width; x++)
            {
                ushort value = (ushort)(row[x * 2] | (row[(x * 2) + 1] << 8));
                byte r = Expand5To8((value >> 11) & 0x1f);
                byte g = Expand6To8((value >> 5) & 0x3f);
                byte b = Expand5To8(value & 0x1f);
                output[outIndex++] = b;
                output[outIndex++] = g;
                output[outIndex++] = r;
                output[outIndex++] = 255;
            }
        }

        return output;
    }

    private static void ValidateDimensions(int width, int height)
    {
        if (width <= 0 || height <= 0)
        {
            throw new InvalidOperationException($"Invalid frame dimensions {width}x{height}.");
        }
    }

    private static string Normalize(string pixelFormat) => pixelFormat.Trim().ToLowerInvariant();

    private static byte Expand5To8(int value) => (byte)((value << 3) | (value >> 2));

    private static byte Expand6To8(int value) => (byte)((value << 2) | (value >> 4));
}
