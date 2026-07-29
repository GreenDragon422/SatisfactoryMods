using System.Buffers.Binary;
using System.Text.Json;

namespace CodexBridge.Companion;

public static class BridgeFrameCodec
{
    public const int ProtocolVersion = 1;
    public const int MaximumPayloadBytes = 1024 * 1024;
    private const int HeaderBytes = sizeof(int);

    public static async ValueTask WriteAsync(
        Stream stream,
        BridgeEnvelope envelope,
        CancellationToken cancellationToken)
    {
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(envelope, BridgeJson.SerializerOptions);
        if (payload.Length > MaximumPayloadBytes)
        {
            throw new BridgeProtocolException(
                $"Payload length {payload.Length} exceeds the maximum {MaximumPayloadBytes} bytes.");
        }

        byte[] header = new byte[HeaderBytes];
        BinaryPrimitives.WriteInt32LittleEndian(header, payload.Length);
        await stream.WriteAsync(header, cancellationToken).ConfigureAwait(false);
        await stream.WriteAsync(payload, cancellationToken).ConfigureAwait(false);
        await stream.FlushAsync(cancellationToken).ConfigureAwait(false);
    }

    public static async ValueTask<BridgeEnvelope?> ReadAsync(
        Stream stream,
        CancellationToken cancellationToken)
    {
        byte[] header = new byte[HeaderBytes];
        int headerBytesRead = await ReadExactAsync(
            stream,
            header,
            allowCleanEndOfStream: true,
            cancellationToken).ConfigureAwait(false);
        if (headerBytesRead == 0)
        {
            return null;
        }

        int payloadLength = BinaryPrimitives.ReadInt32LittleEndian(header);
        if (payloadLength <= 0 || payloadLength > MaximumPayloadBytes)
        {
            throw new BridgeProtocolException(
                $"Frame payload length {payloadLength} is outside the maximum allowed range.");
        }

        byte[] payload = new byte[payloadLength];
        await ReadExactAsync(
            stream,
            payload,
            allowCleanEndOfStream: false,
            cancellationToken).ConfigureAwait(false);

        BridgeEnvelope? envelope;
        try
        {
            envelope = JsonSerializer.Deserialize<BridgeEnvelope>(payload, BridgeJson.SerializerOptions);
        }
        catch (JsonException exception)
        {
            throw new BridgeProtocolException($"Frame JSON is invalid: {exception.Message}");
        }

        if (envelope is null)
        {
            throw new BridgeProtocolException("Frame JSON did not contain an envelope.");
        }

        if (envelope.Version != ProtocolVersion)
        {
            throw new BridgeProtocolException(
                $"Unsupported protocol version {envelope.Version}; expected {ProtocolVersion}.");
        }

        ValidateEnvelope(envelope);

        return envelope;
    }

    private static void ValidateEnvelope(BridgeEnvelope envelope)
    {
        if (!Enum.IsDefined(envelope.Kind))
        {
            throw new BridgeProtocolException($"Message kind {envelope.Kind} is not defined.");
        }

        ValidateRequiredText(envelope.Id, "id", 128);
        ValidateRequiredText(envelope.Role, "role", 32);
        ValidateRequiredText(envelope.Method, "method", 128);
        if (envelope.Role is not ("game" or "mcp" or "service"))
        {
            throw new BridgeProtocolException($"Role '{envelope.Role}' is not supported.");
        }

        if (envelope.Payload.ValueKind is JsonValueKind.Undefined or JsonValueKind.Null)
        {
            throw new BridgeProtocolException("Envelope payload must be a JSON value.");
        }
    }

    private static void ValidateRequiredText(string? value, string fieldName, int maximumLength)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new BridgeProtocolException($"Envelope {fieldName} is required.");
        }

        if (value.Length > maximumLength)
        {
            throw new BridgeProtocolException(
                $"Envelope {fieldName} exceeds {maximumLength} characters.");
        }
    }

    private static async ValueTask<int> ReadExactAsync(
        Stream stream,
        Memory<byte> destination,
        bool allowCleanEndOfStream,
        CancellationToken cancellationToken)
    {
        int totalBytesRead = 0;
        while (totalBytesRead < destination.Length)
        {
            int bytesRead = await stream.ReadAsync(
                destination[totalBytesRead..],
                cancellationToken).ConfigureAwait(false);
            if (bytesRead == 0)
            {
                if (allowCleanEndOfStream && totalBytesRead == 0)
                {
                    return 0;
                }

                throw new BridgeProtocolException("The stream ended before the complete frame was received.");
            }

            totalBytesRead += bytesRead;
        }

        return totalBytesRead;
    }
}
