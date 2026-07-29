using System.Text.Json;
using System.Text.Json.Serialization;

namespace CodexBridge.Companion;

public enum BridgeMessageKind
{
    Hello,
    Request,
    Response,
    Event,
    Heartbeat
}

public enum BridgeRequestEffect
{
    ReadOnly,
    NonDestructive,
    PotentiallyDestructive
}

public sealed class BridgeEnvelope
{
    public required int Version { get; init; }

    public required BridgeMessageKind Kind { get; init; }

    public required string Id { get; init; }

    public required string Role { get; init; }

    public required string Method { get; init; }

    public required JsonElement Payload { get; init; }

    public bool? Ok { get; init; }

    public string? Error { get; init; }
}

public sealed class BridgeResponse
{
    private BridgeResponse(string requestId, bool ok, JsonElement payload, string? error)
    {
        RequestId = requestId;
        Ok = ok;
        Payload = payload;
        Error = error;
    }

    public string RequestId { get; }

    public bool Ok { get; }

    public JsonElement Payload { get; }

    public string? Error { get; }

    public static BridgeResponse Success(string requestId, JsonElement payload)
    {
        return new BridgeResponse(requestId, true, payload, null);
    }

    public static BridgeResponse Failure(string requestId, string error)
    {
        return new BridgeResponse(requestId, false, BridgeJson.EmptyObject, error);
    }
}

public static class BridgeJson
{
    public static readonly JsonSerializerOptions SerializerOptions = CreateSerializerOptions();

    public static JsonElement EmptyObject => ToElement(new { });

    public static JsonElement ToElement<TValue>(TValue value)
    {
        return JsonSerializer.SerializeToElement(value, SerializerOptions);
    }

    private static JsonSerializerOptions CreateSerializerOptions()
    {
        JsonSerializerOptions options = new(JsonSerializerDefaults.Web)
        {
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
            PropertyNameCaseInsensitive = true
        };
        options.Converters.Add(new JsonStringEnumConverter(JsonNamingPolicy.SnakeCaseLower));
        return options;
    }
}

public sealed class BridgeProtocolException : Exception
{
    public BridgeProtocolException(string message)
        : base(message)
    {
    }
}
