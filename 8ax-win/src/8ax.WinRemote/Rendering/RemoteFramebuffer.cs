using System.Windows.Media;
using System.Windows.Media.Imaging;
using EightAxis.WinRemote.Input;

namespace EightAxis.WinRemote.Rendering;

public sealed class RemoteFramebuffer
{
    private readonly object _sync = new();
    private readonly byte[] _bgra32 = new byte[PointerMapper.RemoteWidth * PointerMapper.RemoteHeight * 4];

    public int Width { get; } = PointerMapper.RemoteWidth;

    public int Height { get; } = PointerMapper.RemoteHeight;

    public long FrameId { get; private set; }

    public WriteableBitmap Bitmap { get; } = new(
        PointerMapper.RemoteWidth,
        PointerMapper.RemoteHeight,
        96,
        96,
        PixelFormats.Bgra32,
        null);

    public void ApplyFullFrame(byte[] bgra32Pixels, long frameId)
    {
        int expectedBytes = Width * Height * 4;
        if (bgra32Pixels.Length != expectedBytes)
        {
            throw new ArgumentException($"Expected {expectedBytes} BGRA32 bytes, got {bgra32Pixels.Length}.", nameof(bgra32Pixels));
        }

        lock (_sync)
        {
            Buffer.BlockCopy(bgra32Pixels, 0, _bgra32, 0, expectedBytes);
            if (Bitmap.Dispatcher.CheckAccess())
            {
                Bitmap.WritePixels(
                    new System.Windows.Int32Rect(0, 0, Width, Height),
                    bgra32Pixels,
                    Width * 4,
                    0);
            }

            FrameId = frameId;
        }
    }

    public void ApplyDirtyRect(int x, int y, int width, int height, byte[] bgra32Pixels)
    {
        ValidateRect(x, y, width, height);
        int expectedBytes = width * height * 4;
        if (bgra32Pixels.Length != expectedBytes)
        {
            throw new ArgumentException($"Expected {expectedBytes} BGRA32 rect bytes, got {bgra32Pixels.Length}.", nameof(bgra32Pixels));
        }

        lock (_sync)
        {
            for (int row = 0; row < height; row++)
            {
                int sourceOffset = row * width * 4;
                int targetOffset = (((y + row) * Width) + x) * 4;
                Buffer.BlockCopy(bgra32Pixels, sourceOffset, _bgra32, targetOffset, width * 4);
            }

            if (Bitmap.Dispatcher.CheckAccess())
            {
                Bitmap.WritePixels(
                    new System.Windows.Int32Rect(x, y, width, height),
                    bgra32Pixels,
                    width * 4,
                    0);
            }
        }
    }

    public void SetFrameId(long frameId)
    {
        lock (_sync)
        {
            FrameId = frameId;
        }
    }

    public byte[] CopyBgra32Pixels()
    {
        lock (_sync)
        {
            return _bgra32.ToArray();
        }
    }

    public void ValidateRect(int x, int y, int width, int height)
    {
        if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > Width || y + height > Height)
        {
            throw new InvalidOperationException($"Dirty rect {x},{y},{width},{height} is outside {Width}x{Height}.");
        }
    }
}
