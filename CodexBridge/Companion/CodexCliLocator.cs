using System.Text.Json;

namespace CodexBridge.Companion;

public static class CodexCliLocator
{
    public static string? Find()
    {
        string localApplicationData = Environment.GetFolderPath(
            Environment.SpecialFolder.LocalApplicationData);
        string manifestPath = Path.Combine(
            localApplicationData,
            "OpenAI",
            "Codex",
            "chrome-native-hosts-v2.json");
        string? manifestCandidate = FindFromManifest(manifestPath);
        if (manifestCandidate is not null)
        {
            return manifestCandidate;
        }

        string binaryRoot = Path.Combine(localApplicationData, "OpenAI", "Codex", "bin");
        try
        {
            return Directory.Exists(binaryRoot)
                ? Directory.GetFiles(binaryRoot, "codex.exe", SearchOption.AllDirectories)
                    .OrderByDescending(File.GetLastWriteTimeUtc)
                    .FirstOrDefault()
                : null;
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    public static string? FindFromManifest(string manifestPath)
    {
        if (!File.Exists(manifestPath))
        {
            return null;
        }

        try
        {
            using FileStream stream = File.OpenRead(manifestPath);
            using JsonDocument document = JsonDocument.Parse(stream);
            if (!document.RootElement.TryGetProperty("entries", out JsonElement entries) ||
                entries.ValueKind != JsonValueKind.Array)
            {
                return null;
            }

            return entries.EnumerateArray()
                .Select(entry => new
                {
                    Path = entry.TryGetProperty("paths", out JsonElement paths) &&
                        paths.TryGetProperty("codexCliPath", out JsonElement path)
                            ? path.GetString()
                            : null,
                    UpdatedAt = entry.TryGetProperty("updatedAt", out JsonElement updated) &&
                        DateTimeOffset.TryParse(updated.GetString(), out DateTimeOffset parsed)
                            ? parsed
                            : DateTimeOffset.MinValue
                })
                .Where(candidate => candidate.Path is not null && File.Exists(candidate.Path))
                .OrderByDescending(candidate => candidate.UpdatedAt)
                .Select(candidate => candidate.Path)
                .FirstOrDefault();
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or JsonException)
        {
            return null;
        }
    }
}
