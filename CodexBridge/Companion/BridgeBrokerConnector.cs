using System.Text.Json;

namespace CodexBridge.Companion;

public interface IBridgeBrokerConnection : IAsyncDisposable
{
    Task<BridgeResponse> RequestAsync(
        string method,
        JsonElement payload,
        string requestId,
        long requestSequence,
        CancellationToken cancellationToken);
}

public interface IBridgeBrokerConnector
{
    Task<IBridgeBrokerConnection> ConnectAsync(
        string role,
        string clientInstanceId,
        long generation,
        CancellationToken cancellationToken);
}

public sealed class BridgeBrokerConnector : IBridgeBrokerConnector
{
    private readonly BrokerOptions options;
    private readonly IBridgeServiceProcessLauncher processLauncher;

    public BridgeBrokerConnector(
        BrokerOptions options,
        IBridgeServiceProcessLauncher processLauncher)
    {
        this.options = options;
        this.processLauncher = processLauncher;
    }

    public async Task<IBridgeBrokerConnection> ConnectAsync(
        string role,
        string clientInstanceId,
        long generation,
        CancellationToken cancellationToken)
    {
        return await BrokerServiceLauncher.StartOrConnectAsync(
            options,
            role,
            clientInstanceId,
            generation,
            processLauncher,
            cancellationToken).ConfigureAwait(false);
    }
}
