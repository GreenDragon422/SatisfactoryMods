using System.Text;
using System.Text.Json;

namespace CodexBridge.Companion;

public sealed class CodexCliEventAccumulator
{
    private const int MaximumLineLength = 1024 * 1024;
    private const int MaximumErrorLength = 65536;

    private readonly StringBuilder structuredErrors = new();
    private string? parseError;
    private string? reply;
    private string? threadId;

    public void AddLine(string line)
    {
        if (line.Length > MaximumLineLength)
        {
            parseError = "codex_event_too_large";
            return;
        }

        try
        {
            using JsonDocument document = JsonDocument.Parse(line);
            JsonElement root = document.RootElement;
            string? type = root.TryGetProperty("type", out JsonElement typeElement)
                ? typeElement.GetString()
                : null;
            if (type == "thread.started" &&
                root.TryGetProperty("thread_id", out JsonElement threadElement))
            {
                threadId = threadElement.GetString();
                return;
            }

            if (type == "item.completed" &&
                root.TryGetProperty("item", out JsonElement item))
            {
                string? itemType = item.TryGetProperty("type", out JsonElement itemTypeElement)
                    ? itemTypeElement.GetString()
                    : null;
                if (itemType == "agent_message" &&
                    item.TryGetProperty("text", out JsonElement textElement))
                {
                    reply = textElement.GetString();
                }
                else if (itemType == "error" &&
                    item.TryGetProperty("message", out JsonElement messageElement))
                {
                    AppendError(messageElement.GetString());
                }
            }
            else if (type == "turn.failed" &&
                root.TryGetProperty("error", out JsonElement errorElement))
            {
                if (errorElement.ValueKind == JsonValueKind.String)
                {
                    AppendError(errorElement.GetString());
                }
                else if (errorElement.TryGetProperty("message", out JsonElement messageElement))
                {
                    AppendError(messageElement.GetString());
                }
            }
        }
        catch (JsonException)
        {
            parseError = "codex_event_invalid_json";
        }
    }

    public CodexCliTurnResult Complete(int exitCode, string standardError)
    {
        string normalizedStandardError = TrimError(standardError);
        string error = structuredErrors.Length > 0
            ? structuredErrors.ToString()
            : normalizedStandardError;
        if (parseError is not null)
        {
            error = parseError;
        }

        bool threadNotFound = error.Contains(
            "no rollout found for thread id",
            StringComparison.OrdinalIgnoreCase);
        bool ok = exitCode == 0 &&
            parseError is null &&
            threadId is not null &&
            !string.IsNullOrWhiteSpace(reply);
        if (!ok && string.IsNullOrWhiteSpace(error))
        {
            error = exitCode == 0
                ? "codex_reply_incomplete"
                : $"codex_exit_{exitCode}";
        }

        return new CodexCliTurnResult(
            ok,
            threadId,
            reply,
            ok ? null : error,
            threadNotFound);
    }

    private void AppendError(string? error)
    {
        if (string.IsNullOrWhiteSpace(error) || structuredErrors.Length >= MaximumErrorLength)
        {
            return;
        }

        if (structuredErrors.Length > 0)
        {
            structuredErrors.AppendLine();
        }

        int remaining = MaximumErrorLength - structuredErrors.Length;
        structuredErrors.Append(error.AsSpan(0, Math.Min(error.Length, remaining)));
    }

    private static string TrimError(string error)
    {
        string normalized = error.Trim();
        return normalized.Length <= MaximumErrorLength
            ? normalized
            : normalized[^MaximumErrorLength..];
    }
}
