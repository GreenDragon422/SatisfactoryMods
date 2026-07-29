using System.Text.Json;

namespace CodexBridge.Companion;

public sealed class BrokerBridgeToolTransport : IBridgeToolTransport, IDisposable
{
    private readonly IBridgeBrokerConnector connector;
    private readonly SemaphoreSlim requestGate = new(1, 1);
    private readonly string clientInstanceId = Guid.NewGuid().ToString("N");
    private long sequence;

    public BrokerBridgeToolTransport(
        IBridgeBrokerConnector connector)
    {
        this.connector = connector;
    }

    public async Task<BridgeResponse> RequestAsync(
        string method,
        JsonElement payload,
        string requestId,
        BridgeRequestEffect effect,
        CancellationToken cancellationToken)
    {
        await requestGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            long requestSequence = Interlocked.Increment(ref sequence);
            int maximumAttempts = effect == BridgeRequestEffect.ReadOnly ? 2 : 1;
            for (int attempt = 1; attempt <= maximumAttempts; attempt++)
            {
                bool connected = false;
                try
                {
                    await using IBridgeBrokerConnection client = await connector.ConnectAsync(
                        "mcp",
                        clientInstanceId,
                        0,
                        cancellationToken).ConfigureAwait(false);
                    connected = true;
                    BridgeResponse response = await client.RequestAsync(
                        method,
                        payload,
                        requestId,
                        requestSequence,
                        cancellationToken).ConfigureAwait(false);
                    if (effect == BridgeRequestEffect.ReadOnly &&
                        attempt < maximumAttempts &&
                        IsAmbiguous(response.Error))
                    {
                        continue;
                    }

                    if (effect != BridgeRequestEffect.ReadOnly && IsAmbiguous(response.Error))
                    {
                        return BridgeResponse.Failure(requestId, "delivery_unknown");
                    }

                    return response;
                }
                catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
                {
                    throw;
                }
                catch (Exception exception) when (
                    exception is IOException or TimeoutException or BridgeProtocolException)
                {
                    if (effect == BridgeRequestEffect.ReadOnly && attempt < maximumAttempts)
                    {
                        continue;
                    }

                    string error = connected && effect != BridgeRequestEffect.ReadOnly
                        ? "delivery_unknown"
                        : "bridge_unavailable";
                    return BridgeResponse.Failure(requestId, error);
                }
            }

            return BridgeResponse.Failure(requestId, "bridge_unavailable");
        }
        finally
        {
            requestGate.Release();
        }
    }

    public void Dispose()
    {
        requestGate.Dispose();
    }

    private static bool IsAmbiguous(string? error)
    {
        return error is "broker_disconnected" or "delivery_unknown" or "game_timeout";
    }
}
