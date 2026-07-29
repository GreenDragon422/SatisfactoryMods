using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace CodexBridge.Companion;

public sealed class BridgeDiagnosticLog
{
    private const long MaximumLogBytes = 2 * 1024 * 1024;
    private readonly object synchronization = new();
    private readonly string logPath;
    private readonly JsonSerializerOptions serializerOptions = new()
    {
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
    };

    public BridgeDiagnosticLog(string stateDirectory)
    {
        Directory.CreateDirectory(stateDirectory);
        logPath = Path.Combine(stateDirectory, "bridge.log");
    }

    public void Write(
        string eventName,
        string? role = null,
        string? clientInstanceId = null,
        string? correlationId = null,
        string? method = null,
        string? reason = null,
        long? durationMilliseconds = null,
        JsonElement? requestPayload = null)
    {
        CommandMetadata? command = method == "console.execute" && requestPayload is not null
            ? CreateCommandMetadata(requestPayload.Value)
            : null;
        DiagnosticEntry entry = new(
            DateTimeOffset.UtcNow,
            eventName,
            role,
            clientInstanceId,
            correlationId,
            method,
            reason,
            durationMilliseconds,
            command?.CommandName,
            command?.ArgumentLength,
            command?.CommandHash);
        string line = JsonSerializer.Serialize(entry, serializerOptions) + Environment.NewLine;
        lock (synchronization)
        {
            try
            {
                RotateIfNeeded(Encoding.UTF8.GetByteCount(line));
                File.AppendAllText(logPath, line, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
            }
            catch (Exception exception) when (
                exception is IOException or UnauthorizedAccessException)
            {
                Console.Error.WriteLine(
                    $"CodexBridge diagnostic logging failed: {exception.GetType().Name}");
            }
        }
    }

    private void RotateIfNeeded(int incomingBytes)
    {
        FileInfo current = new(logPath);
        if (!current.Exists || current.Length + incomingBytes <= MaximumLogBytes)
        {
            return;
        }

        string rotatedPath = logPath + ".1";
        File.Delete(rotatedPath);
        File.Move(logPath, rotatedPath);
    }

    private static CommandMetadata? CreateCommandMetadata(JsonElement payload)
    {
        if (payload.ValueKind != JsonValueKind.Object ||
            !payload.TryGetProperty("command", out JsonElement commandElement) ||
            commandElement.ValueKind != JsonValueKind.String)
        {
            return null;
        }

        string? command = commandElement.GetString();
        if (string.IsNullOrWhiteSpace(command))
        {
            return null;
        }

        int separator = -1;
        for (int index = 0; index < command.Length; index++)
        {
            if (char.IsWhiteSpace(command[index]))
            {
                separator = index;
                break;
            }
        }

        string commandName = separator < 0 ? command : command[..separator];
        int argumentStart = separator;
        while (argumentStart >= 0 &&
            argumentStart < command.Length &&
            char.IsWhiteSpace(command[argumentStart]))
        {
            argumentStart++;
        }

        int argumentLength = separator < 0 ? 0 : command.Length - argumentStart;
        string commandHash = Convert.ToHexString(
            SHA256.HashData(Encoding.UTF8.GetBytes(command)));
        return new CommandMetadata(commandName, argumentLength, commandHash);
    }

    private sealed record DiagnosticEntry(
        DateTimeOffset Timestamp,
        string Event,
        string? Role,
        string? ClientInstanceId,
        string? CorrelationId,
        string? Method,
        string? Reason,
        long? DurationMilliseconds,
        string? CommandName,
        int? ArgumentLength,
        string? CommandHash);

    private sealed record CommandMetadata(
        string CommandName,
        int ArgumentLength,
        string CommandHash);
}
