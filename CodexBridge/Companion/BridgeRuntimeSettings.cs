using System.Text.Json;

namespace CodexBridge.Companion;

public sealed record BridgeRuntimeSettings(string? WorkspaceDirectory)
{
    public static BridgeRuntimeSettings Load(string? companionDirectory = null)
    {
        string directory = companionDirectory ??
            Path.GetDirectoryName(Environment.ProcessPath) ??
            AppContext.BaseDirectory;
        string settingsPath = Path.Combine(directory, "bridge.settings.json");
        if (!File.Exists(settingsPath))
        {
            return new BridgeRuntimeSettings((string?)null);
        }

        try
        {
            using FileStream stream = File.OpenRead(settingsPath);
            using JsonDocument document = JsonDocument.Parse(stream);
            if (!document.RootElement.TryGetProperty(
                    "workspacePath",
                    out JsonElement workspaceElement))
            {
                return new BridgeRuntimeSettings((string?)null);
            }

            string? workspacePath = workspaceElement.GetString();
            if (string.IsNullOrWhiteSpace(workspacePath) ||
                !Path.IsPathFullyQualified(workspacePath) ||
                !Directory.Exists(workspacePath))
            {
                return new BridgeRuntimeSettings((string?)null);
            }

            return new BridgeRuntimeSettings(Path.GetFullPath(workspacePath));
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or JsonException)
        {
            return new BridgeRuntimeSettings((string?)null);
        }
    }
}
