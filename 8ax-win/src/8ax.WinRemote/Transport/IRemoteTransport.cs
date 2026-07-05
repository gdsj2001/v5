using EightAxis.WinRemote.Protocol;

namespace EightAxis.WinRemote.Transport;

public interface IRemoteTransport
{
    Uri BaseUri { get; }

    Task<RemoteInfoMessage> GetInfoAsync(CancellationToken cancellationToken);

    Task<RemoteFramePacket> GetFullFrameAsync(CancellationToken cancellationToken);

    IAsyncEnumerable<RemoteFramePacket> ReadFrameStreamAsync(CancellationToken cancellationToken);
}
