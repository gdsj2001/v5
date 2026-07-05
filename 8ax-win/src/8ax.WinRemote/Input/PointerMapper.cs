using System.Windows;

namespace EightAxis.WinRemote.Input;

public readonly record struct RemotePoint(int X, int Y, bool IsInside);

public static class PointerMapper
{
    public const int RemoteWidth = 1024;
    public const int RemoteHeight = 600;

    public static RemotePoint Map(Point localPoint, Size viewportSize)
    {
        if (viewportSize.Width <= 0 || viewportSize.Height <= 0)
        {
            return new RemotePoint(0, 0, false);
        }

        double scale = Math.Min(viewportSize.Width / RemoteWidth, viewportSize.Height / RemoteHeight);
        if (scale <= 0)
        {
            return new RemotePoint(0, 0, false);
        }

        double displayWidth = RemoteWidth * scale;
        double displayHeight = RemoteHeight * scale;
        double offsetX = (viewportSize.Width - displayWidth) * 0.5;
        double offsetY = (viewportSize.Height - displayHeight) * 0.5;
        double remoteX = (localPoint.X - offsetX) / scale;
        double remoteY = (localPoint.Y - offsetY) / scale;

        bool inside = remoteX >= 0 && remoteY >= 0 && remoteX < RemoteWidth && remoteY < RemoteHeight;
        if (!inside)
        {
            return new RemotePoint(0, 0, false);
        }

        return new RemotePoint(
            Math.Clamp((int)Math.Floor(remoteX), 0, RemoteWidth - 1),
            Math.Clamp((int)Math.Floor(remoteY), 0, RemoteHeight - 1),
            true);
    }
}
