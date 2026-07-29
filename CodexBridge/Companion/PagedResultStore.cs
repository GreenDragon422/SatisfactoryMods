using System.Text;

namespace CodexBridge.Companion;

public sealed record PagedBridgeResult(
    string Text,
    bool Truncated,
    string? ResultId,
    string? NextCursor);

public sealed class PagedResultStore
{
    private readonly object synchronization = new();
    private readonly Dictionary<string, StoredResult> results = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Continuation> continuations = new(StringComparer.Ordinal);
    private readonly int pageBytes;
    private readonly TimeSpan lifetime;
    private readonly TimeProvider timeProvider;
    private readonly int maximumResults;
    private readonly int maximumStoredBytes;
    private int storedBytes;

    public PagedResultStore(
        int pageBytes,
        TimeSpan lifetime,
        TimeProvider timeProvider,
        int maximumResults = 64,
        int maximumStoredBytes = 4 * 1024 * 1024)
    {
        if (pageBytes < 4)
        {
            throw new ArgumentOutOfRangeException(nameof(pageBytes));
        }

        if (maximumResults <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(maximumResults));
        }

        if (maximumStoredBytes < pageBytes)
        {
            throw new ArgumentOutOfRangeException(nameof(maximumStoredBytes));
        }

        this.pageBytes = pageBytes;
        this.lifetime = lifetime;
        this.timeProvider = timeProvider;
        this.maximumResults = maximumResults;
        this.maximumStoredBytes = maximumStoredBytes;
    }

    public PagedBridgeResult Store(string text)
    {
        ArgumentNullException.ThrowIfNull(text);
        lock (synchronization)
        {
            RemoveExpiredResults();
            int textBytes = Encoding.UTF8.GetByteCount(text);
            if (textBytes <= pageBytes)
            {
                return new PagedBridgeResult(text, false, null, null);
            }

            if (results.Count >= maximumResults || storedBytes + textBytes > maximumStoredBytes)
            {
                throw new PagedResultException("The paged result store is busy and at capacity.");
            }

            string resultId = Guid.NewGuid().ToString("N");
            DateTimeOffset expiresAt = timeProvider.GetUtcNow().Add(lifetime);
            StoredResult result = new(text, textBytes, expiresAt);
            results.Add(resultId, result);
            storedBytes += textBytes;
            return CreatePage(resultId, result, 0);
        }
    }

    public PagedBridgeResult Read(string cursor)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(cursor);
        lock (synchronization)
        {
            RemoveExpiredResults();
            if (!continuations.Remove(cursor, out Continuation? continuation) ||
                !results.TryGetValue(continuation.ResultId, out StoredResult? result))
            {
                throw new PagedResultException("The continuation cursor is invalid or has expired.");
            }

            return CreatePage(continuation.ResultId, result, continuation.Offset);
        }
    }

    private PagedBridgeResult CreatePage(string resultId, StoredResult result, int offset)
    {
        int nextOffset = offset;
        int usedBytes = 0;
        while (nextOffset < result.Text.Length)
        {
            Rune rune = Rune.GetRuneAt(result.Text, nextOffset);
            if (usedBytes + rune.Utf8SequenceLength > pageBytes)
            {
                break;
            }

            usedBytes += rune.Utf8SequenceLength;
            nextOffset += rune.Utf16SequenceLength;
        }

        string page = result.Text[offset..nextOffset];
        bool truncated = nextOffset < result.Text.Length;
        if (!truncated)
        {
            RemoveResult(resultId, result);
            return new PagedBridgeResult(page, false, resultId, null);
        }

        string nextCursor = Guid.NewGuid().ToString("N");
        continuations.Add(nextCursor, new Continuation(resultId, nextOffset));
        return new PagedBridgeResult(page, true, resultId, nextCursor);
    }

    private void RemoveExpiredResults()
    {
        DateTimeOffset now = timeProvider.GetUtcNow();
        string[] expiredResultIds = results
            .Where(pair => pair.Value.ExpiresAt <= now)
            .Select(pair => pair.Key)
            .ToArray();
        foreach (string resultId in expiredResultIds)
        {
            StoredResult result = results[resultId];
            RemoveResult(resultId, result);
        }
    }

    private void RemoveResult(string resultId, StoredResult result)
    {
        results.Remove(resultId);
        storedBytes -= result.Utf8Bytes;
        string[] staleCursors = continuations
            .Where(pair => pair.Value.ResultId == resultId)
            .Select(pair => pair.Key)
            .ToArray();
        foreach (string cursor in staleCursors)
        {
            continuations.Remove(cursor);
        }
    }

    private sealed record StoredResult(
        string Text,
        int Utf8Bytes,
        DateTimeOffset ExpiresAt);

    private sealed record Continuation(string ResultId, int Offset);
}

public sealed class PagedResultException : Exception
{
    public PagedResultException(string message)
        : base(message)
    {
    }
}
