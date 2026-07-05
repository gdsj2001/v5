using System.Buffers.Binary;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace EightAxis.WinRemote.Protocol;

public static class RemoteProtocolJson
{
    public static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        PropertyNameCaseInsensitive = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    public static string Serialize<T>(T message) => JsonSerializer.Serialize(message, Options);

    public static T Deserialize<T>(string json)
    {
        T? message = JsonSerializer.Deserialize<T>(json, Options);
        return message ?? throw new InvalidOperationException("Protocol JSON decoded to null.");
    }

    public static byte[] EncodeFrameEnvelope(FrameMetadata metadata, ReadOnlySpan<byte> payload)
    {
        byte[] metadataBytes = Encoding.UTF8.GetBytes(Serialize(metadata));
        byte[] envelope = new byte[4 + metadataBytes.Length + payload.Length];
        BinaryPrimitives.WriteInt32LittleEndian(envelope.AsSpan(0, 4), metadataBytes.Length);
        metadataBytes.CopyTo(envelope.AsSpan(4));
        payload.CopyTo(envelope.AsSpan(4 + metadataBytes.Length));
        return envelope;
    }

    public static RemoteFramePacket DecodeFrameEnvelope(ReadOnlySpan<byte> envelope)
    {
        if (envelope.Length < 4)
        {
            throw new InvalidOperationException("Frame envelope is shorter than the metadata length prefix.");
        }

        int metadataLength = BinaryPrimitives.ReadInt32LittleEndian(envelope[..4]);
        if (metadataLength <= 0 || 4 + metadataLength > envelope.Length)
        {
            throw new InvalidOperationException("Frame envelope metadata length is invalid.");
        }

        string json = Encoding.UTF8.GetString(envelope.Slice(4, metadataLength));
        FrameMetadata metadata = Deserialize<FrameMetadata>(json);
        byte[] payload = envelope[(4 + metadataLength)..].ToArray();
        return new RemoteFramePacket(metadata, payload);
    }
}
