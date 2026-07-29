namespace CodexBridge.Companion;

public sealed record RequestIdentity(
    string ClientInstanceId,
    long Sequence,
    string RequestId,
    long GameConnectionGeneration);

public enum RequestStartDisposition
{
    Accepted,
    InProgress,
    ReplayCompleted,
    DeliveryUnknown,
    Busy,
    StaleSequence
}

public sealed record RequestStartResult(
    RequestStartDisposition Disposition,
    BridgeResponse? CachedResponse = null);

public sealed class RequestLedger
{
    private readonly object synchronization = new();
    private readonly Dictionary<RequestIdentity, RequestEntry> requests = new(
        RequestIdentityComparer.Instance);
    private readonly Dictionary<ClientLeaseKey, LeaseSequenceEntry> highestSequences = new();
    private readonly int maximumEntries;
    private readonly TimeSpan lifetime;
    private readonly TimeProvider timeProvider;
    private readonly int maximumLeaseEntries;
    private readonly TimeSpan leaseSequenceLifetime;

    public RequestLedger(
        int maximumEntries = 4096,
        TimeSpan? lifetime = null,
        TimeProvider? timeProvider = null,
        int maximumLeaseEntries = 1024,
        TimeSpan? leaseSequenceLifetime = null)
    {
        if (maximumEntries <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(maximumEntries));
        }

        if (maximumLeaseEntries <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(maximumLeaseEntries));
        }

        this.maximumEntries = maximumEntries;
        this.lifetime = lifetime ?? TimeSpan.FromMinutes(2);
        this.timeProvider = timeProvider ?? TimeProvider.System;
        this.maximumLeaseEntries = maximumLeaseEntries;
        this.leaseSequenceLifetime = leaseSequenceLifetime ?? TimeSpan.FromMinutes(30);
    }

    public RequestStartResult TryBegin(RequestIdentity identity, BridgeRequestEffect effect)
    {
        ValidateIdentity(identity);
        lock (synchronization)
        {
            RemoveExpiredEntries();
            if (requests.TryGetValue(identity, out RequestEntry? entry))
            {
                return BeginExisting(identity, entry);
            }

            ClientLeaseKey leaseKey = new(identity.ClientInstanceId);
            if (highestSequences.TryGetValue(leaseKey, out LeaseSequenceEntry? leaseSequence) &&
                identity.Sequence <= leaseSequence.HighestSequence)
            {
                return new RequestStartResult(RequestStartDisposition.StaleSequence);
            }

            if (requests.Count >= maximumEntries ||
                (!highestSequences.ContainsKey(leaseKey) &&
                 highestSequences.Count >= maximumLeaseEntries))
            {
                return new RequestStartResult(RequestStartDisposition.Busy);
            }

            requests.Add(identity, new RequestEntry(effect, RequestState.InProgress, null, null));
            highestSequences[leaseKey] = new LeaseSequenceEntry(
                identity.Sequence,
                timeProvider.GetUtcNow().Add(leaseSequenceLifetime));
            return new RequestStartResult(RequestStartDisposition.Accepted);
        }
    }

    public bool MarkCompleted(RequestIdentity identity, BridgeResponse response)
    {
        ArgumentNullException.ThrowIfNull(response);
        lock (synchronization)
        {
            if (!requests.TryGetValue(identity, out RequestEntry? entry) ||
                entry.State != RequestState.InProgress)
            {
                return false;
            }

            requests[identity] = entry with
            {
                State = RequestState.Completed,
                Response = response,
                ExpiresAt = timeProvider.GetUtcNow().Add(lifetime)
            };
            return true;
        }
    }

    public bool MarkConnectionLost(RequestIdentity identity)
    {
        lock (synchronization)
        {
            if (!requests.TryGetValue(identity, out RequestEntry? entry) ||
                entry.State != RequestState.InProgress)
            {
                return false;
            }

            if (entry.Effect == BridgeRequestEffect.ReadOnly)
            {
                requests[identity] = entry with
                {
                    State = RequestState.Retryable,
                    Response = null,
                    ExpiresAt = timeProvider.GetUtcNow().Add(lifetime)
                };
                return true;
            }

            requests[identity] = entry with
            {
                State = RequestState.DeliveryUnknown,
                Response = null,
                ExpiresAt = timeProvider.GetUtcNow().Add(lifetime)
            };
            return true;
        }
    }

    private RequestStartResult BeginExisting(RequestIdentity identity, RequestEntry entry)
    {
        switch (entry.State)
        {
            case RequestState.Retryable:
                requests[identity] = entry with
                {
                    State = RequestState.InProgress,
                    ExpiresAt = null
                };
                return new RequestStartResult(RequestStartDisposition.Accepted);

            case RequestState.InProgress:
                return new RequestStartResult(RequestStartDisposition.InProgress);

            case RequestState.Completed:
                return new RequestStartResult(
                    RequestStartDisposition.ReplayCompleted,
                    entry.Response);

            case RequestState.DeliveryUnknown:
                return new RequestStartResult(RequestStartDisposition.DeliveryUnknown);

            default:
                throw new InvalidOperationException($"Unsupported request state {entry.State}.");
        }
    }

    private void RemoveExpiredEntries()
    {
        DateTimeOffset now = timeProvider.GetUtcNow();
        RequestIdentity[] expiredIdentities = requests
            .Where(pair => pair.Value.ExpiresAt is not null && pair.Value.ExpiresAt <= now)
            .Select(pair => pair.Key)
            .ToArray();
        foreach (RequestIdentity identity in expiredIdentities)
        {
            requests.Remove(identity);
        }

        ClientLeaseKey[] inactiveLeaseKeys = highestSequences
            .Where(pair => pair.Value.ExpiresAt <= now)
            .Select(pair => pair.Key)
            .ToArray();
        foreach (ClientLeaseKey leaseKey in inactiveLeaseKeys)
        {
            highestSequences.Remove(leaseKey);
        }
    }

    private static void ValidateIdentity(RequestIdentity identity)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(identity.ClientInstanceId);
        ArgumentException.ThrowIfNullOrWhiteSpace(identity.RequestId);
        if (identity.Sequence <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(identity.Sequence));
        }

        if (identity.GameConnectionGeneration < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(identity.GameConnectionGeneration));
        }
    }

    private enum RequestState
    {
        InProgress,
        Retryable,
        Completed,
        DeliveryUnknown
    }

    private sealed record RequestEntry(
        BridgeRequestEffect Effect,
        RequestState State,
        BridgeResponse? Response,
        DateTimeOffset? ExpiresAt);

    private sealed record ClientLeaseKey(string ClientInstanceId);

    private sealed record LeaseSequenceEntry(long HighestSequence, DateTimeOffset ExpiresAt);

    private sealed class RequestIdentityComparer : IEqualityComparer<RequestIdentity>
    {
        public static RequestIdentityComparer Instance { get; } = new();

        public bool Equals(RequestIdentity? left, RequestIdentity? right)
        {
            if (ReferenceEquals(left, right))
            {
                return true;
            }

            if (left is null || right is null)
            {
                return false;
            }

            return left.Sequence == right.Sequence &&
                string.Equals(left.ClientInstanceId, right.ClientInstanceId, StringComparison.Ordinal) &&
                string.Equals(left.RequestId, right.RequestId, StringComparison.Ordinal);
        }

        public int GetHashCode(RequestIdentity identity)
        {
            return HashCode.Combine(
                StringComparer.Ordinal.GetHashCode(identity.ClientInstanceId),
                identity.Sequence,
                StringComparer.Ordinal.GetHashCode(identity.RequestId));
        }
    }
}
