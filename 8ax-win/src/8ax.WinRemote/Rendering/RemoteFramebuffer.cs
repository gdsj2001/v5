using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Runtime.InteropServices;
using EightAxis.WinRemote.Input;

namespace EightAxis.WinRemote.Rendering;

public sealed class RemoteFramebuffer
{
    private readonly object _sync = new();
    private readonly byte[] _bgra32 = new byte[PointerMapper.RemoteWidth * PointerMapper.RemoteHeight * 4];

    public int Width { get; } = PointerMapper.RemoteWidth;

    public int Height { get; } = PointerMapper.RemoteHeight;

    public long FrameId { get; private set; }

    public int BitmapCommitCount { get; private set; }

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
                BitmapCommitCount++;
            }

            FrameId = frameId;
        }
    }

    public void ApplyDirtyRects(IReadOnlyList<RemoteDirtyRectUpdate> updates, long frameId)
    {
        ArgumentNullException.ThrowIfNull(updates);
        foreach (RemoteDirtyRectUpdate update in updates)
        {
            ValidateRect(update.X, update.Y, update.Width, update.Height);
            int expectedBytes = checked(update.Width * update.Height * 4);
            if (update.Bgra32Pixels.Length != expectedBytes)
            {
                throw new ArgumentException($"Expected {expectedBytes} BGRA32 rect bytes, got {update.Bgra32Pixels.Length}.", nameof(updates));
            }
        }

        lock (_sync)
        {
            if (updates.Count == 0)
            {
                FrameId = frameId;
                return;
            }

            foreach (RemoteDirtyRectUpdate update in updates)
            {
                for (int row = 0; row < update.Height; row++)
                {
                    int sourceOffset = row * update.Width * 4;
                    int targetOffset = (((update.Y + row) * Width) + update.X) * 4;
                    Buffer.BlockCopy(update.Bgra32Pixels, sourceOffset, _bgra32, targetOffset, update.Width * 4);
                }
            }

            if (Bitmap.Dispatcher.CheckAccess())
            {
                Bitmap.Lock();
                try
                {
                    foreach (RemoteDirtyRectUpdate update in updates)
                    {
                        int rowBytes = checked(update.Width * 4);
                        for (int row = 0; row < update.Height; row++)
                        {
                            int sourceOffset = row * rowBytes;
                            IntPtr target = IntPtr.Add(
                                Bitmap.BackBuffer,
                                checked(((update.Y + row) * Bitmap.BackBufferStride) + (update.X * 4)));
                            Marshal.Copy(update.Bgra32Pixels, sourceOffset, target, rowBytes);
                        }

                        Bitmap.AddDirtyRect(new System.Windows.Int32Rect(update.X, update.Y, update.Width, update.Height));
                    }
                }
                finally
                {
                    Bitmap.Unlock();
                }

                BitmapCommitCount++;
            }

            FrameId = frameId;
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

public sealed record RemoteDirtyRectUpdate(int X, int Y, int Width, int Height, byte[] Bgra32Pixels);
