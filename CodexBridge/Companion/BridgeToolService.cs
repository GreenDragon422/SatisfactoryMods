using System.Text.Json;

namespace CodexBridge.Companion;

public interface IBridgeToolTransport
{
    Task<BridgeResponse> RequestAsync(
        string method,
        JsonElement payload,
        string requestId,
        BridgeRequestEffect effect,
        CancellationToken cancellationToken);
}

public sealed record BridgeToolResult(
    string RequestId,
    bool Ok,
    string? Error,
    JsonElement Payload);

public sealed class BridgeToolService
{
    private readonly IBridgeToolTransport transport;

    public BridgeToolService(IBridgeToolTransport transport)
    {
        this.transport = transport;
    }

    public Task<BridgeToolResult> GameStatusAsync(CancellationToken cancellationToken)
    {
        return InvokeAsync(
            "game.status",
            BridgeJson.EmptyObject,
            null,
            BridgeRequestEffect.ReadOnly,
            cancellationToken);
    }

    public Task<BridgeToolResult> DiscoverConsoleCommandsAsync(
        string? query,
        int limit,
        string? cursor,
        CancellationToken cancellationToken)
    {
        if (query is not null && query.Length > 256)
        {
            throw new ArgumentException("The discovery query may contain at most 256 characters.", nameof(query));
        }

        if (limit is < 1 or > 100)
        {
            throw new ArgumentOutOfRangeException(
                nameof(limit),
                "The discovery page limit must be between 1 and 100.");
        }


        string? normalizedCursor = cursor is null
            ? null
            : RequireOpaqueToken(cursor, nameof(cursor));

        return InvokeAsync(
            "console.discover",
            BridgeJson.ToElement(new
            {
                query = query ?? string.Empty,
                limit,
                cursor = normalizedCursor
            }),
            null,
            BridgeRequestEffect.ReadOnly,
            cancellationToken);
    }

    public Task<BridgeToolResult> ExecuteConsoleCommandAsync(
        string command,
        string? requestId,
        CancellationToken cancellationToken)
    {
        string normalizedCommand = RequireText(command, 4096, nameof(command));
        return InvokeAsync(
            "console.execute",
            BridgeJson.ToElement(new { command = normalizedCommand }),
            requestId,
            BridgeRequestEffect.PotentiallyDestructive,
            cancellationToken);
    }

    public Task<BridgeToolResult> SendGameMessageAsync(
        string message,
        CancellationToken cancellationToken)
    {
        string normalizedMessage = RequireText(message, 8192, nameof(message));
        return InvokeAsync(
            "game.message",
            BridgeJson.ToElement(new { message = normalizedMessage }),
            null,
            BridgeRequestEffect.NonDestructive,
            cancellationToken);
    }

    public Task<BridgeToolResult> ReadRecentGameLogAsync(
        int maxLines,
        CancellationToken cancellationToken)
    {
        if (maxLines is < 1 or > 500)
        {
            throw new ArgumentOutOfRangeException(
                nameof(maxLines),
                "The log line limit must be between 1 and 500.");
        }

        return InvokeAsync(
            "log.recent",
            BridgeJson.ToElement(new { maxLines }),
            null,
            BridgeRequestEffect.ReadOnly,
            cancellationToken);
    }

    public Task<BridgeToolResult> ReadResultPageAsync(
        string resultId,
        string cursor,
        CancellationToken cancellationToken)
    {
        string normalizedResultId = RequireOpaqueToken(resultId, nameof(resultId));
        string normalizedCursor = RequireOpaqueToken(cursor, nameof(cursor));
        return InvokeAsync(
            "result.read",
            BridgeJson.ToElement(new
            {
                resultId = normalizedResultId,
                cursor = normalizedCursor
            }),
            null,
            BridgeRequestEffect.ReadOnly,
            cancellationToken);
    }

    public Task<BridgeToolResult> BridgeConversationAsync(
        bool reset,
        CancellationToken cancellationToken)
    {
        return InvokeAsync(
            "bridge.conversation",
            BridgeJson.ToElement(new { reset }),
            null,
            BridgeRequestEffect.NonDestructive,
            cancellationToken);
    }

    private async Task<BridgeToolResult> InvokeAsync(
        string method,
        JsonElement payload,
        string? requestId,
        BridgeRequestEffect effect,
        CancellationToken cancellationToken)
    {
        string normalizedRequestId = NormalizeRequestId(requestId);
        BridgeResponse response = await transport.RequestAsync(
            method,
            payload,
            normalizedRequestId,
            effect,
            cancellationToken).ConfigureAwait(false);
        return new BridgeToolResult(
            response.RequestId,
            response.Ok,
            response.Error,
            response.Payload);
    }

    private static string RequireText(string value, int maximumLength, string parameterName)
    {
        string normalized = value.Trim();
        if (normalized.Length == 0 || normalized.Length > maximumLength)
        {
            throw new ArgumentException(
                $"{parameterName} must contain between 1 and {maximumLength} characters.",
                parameterName);
        }

        return normalized;
    }

    private static string NormalizeRequestId(string? requestId)
    {
        if (string.IsNullOrWhiteSpace(requestId))
        {
            return Guid.NewGuid().ToString("N");
        }

        string normalized = requestId.Trim();
        if (normalized.Length > 128 || normalized.Any(character =>
                !char.IsAsciiLetterOrDigit(character) &&
                character is not '.' and not '_' and not ':' and not '-'))
        {
            throw new ArgumentException(
                "requestId must be 1-128 ASCII letters, digits, dots, underscores, colons, or hyphens.",
                nameof(requestId));
        }

        return normalized;
    }

    private static string RequireOpaqueToken(string value, string parameterName)
    {
        string normalized = RequireText(value, 128, parameterName);
        if (normalized.Any(character => !char.IsAsciiLetterOrDigit(character)))
        {
            throw new ArgumentException(
                $"{parameterName} must contain only ASCII letters and digits.",
                parameterName);
        }

        return normalized;
    }
}
