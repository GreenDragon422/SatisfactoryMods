using System.Text;
using System.Text.Json;

namespace CodexBridge.Companion;

public sealed class FileConversationStateStore : IConversationStateStore
{
    private readonly string stateDirectory;
    private readonly string statePath;

    public FileConversationStateStore(string stateDirectory)
    {
        this.stateDirectory = stateDirectory;
        statePath = Path.Combine(stateDirectory, "conversation.json");
    }

    public string? ReadThreadId()
    {
        if (!File.Exists(statePath))
        {
            return null;
        }

        try
        {
            string json = File.ReadAllText(statePath, Encoding.UTF8);
            ConversationState? state = JsonSerializer.Deserialize<ConversationState>(
                json,
                BridgeJson.SerializerOptions);
            if (state?.ThreadId is null || !Guid.TryParse(state.ThreadId, out Guid _))
            {
                QuarantineCorruptState();
                return null;
            }

            return state.ThreadId;
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or JsonException)
        {
            QuarantineCorruptState();
            return null;
        }
    }

    public void WriteThreadId(string threadId)
    {
        if (!Guid.TryParse(threadId, out Guid _))
        {
            throw new ArgumentException("The Codex thread ID must be a UUID.", nameof(threadId));
        }

        Directory.CreateDirectory(stateDirectory);
        string temporaryPath = Path.Combine(
            stateDirectory,
            $"conversation.{Guid.NewGuid():N}.tmp");
        string json = JsonSerializer.Serialize(
            new ConversationState(threadId, DateTimeOffset.UtcNow),
            BridgeJson.SerializerOptions);
        try
        {
            File.WriteAllText(temporaryPath, json, new UTF8Encoding(false));
            File.Move(temporaryPath, statePath, true);
        }
        finally
        {
            File.Delete(temporaryPath);
        }
    }

    public void Clear()
    {
        File.Delete(statePath);
    }

    private void QuarantineCorruptState()
    {
        if (!File.Exists(statePath))
        {
            return;
        }

        string quarantinePath = Path.Combine(
            stateDirectory,
            $"conversation.json.corrupt-{DateTimeOffset.UtcNow:yyyyMMddHHmmss}-{Guid.NewGuid():N}");
        try
        {
            File.Move(statePath, quarantinePath);
        }
        catch (IOException)
        {
            File.Delete(statePath);
        }
        catch (UnauthorizedAccessException)
        {
            File.Delete(statePath);
        }
    }

    private sealed record ConversationState(
        string ThreadId,
        DateTimeOffset UpdatedAt);
}
