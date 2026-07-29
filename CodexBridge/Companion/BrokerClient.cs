using System.Diagnostics;
using System.IO.Pipes;
using System.Text.Json;

namespace CodexBridge.Companion;

public sealed class BrokerClient : IBridgeBrokerConnection
{
    private readonly NamedPipeClientStream pipe;
    private readonly SemaphoreSlim writeLock = new(1, 1);
    private readonly SemaphoreSlim requestLock = new(1, 1);
    private readonly string role;
    private readonly string clientInstanceId;
    private long sequence;

    private BrokerClient(
        NamedPipeClientStream pipe,
        string role,
        string clientInstanceId,
        long generation)
    {
        this.pipe = pipe;
        this.role = role;
        this.clientInstanceId = clientInstanceId;
        Generation = generation;
    }

    public long Generation { get; }

    public static async Task<BrokerClient> ConnectAsync(
        BrokerOptions options,
        string role,
        string clientInstanceId,
        long generation,
        string handshakeToken,
        CancellationToken cancellationToken)
    {
        NamedPipeClientStream pipe = new(
            ".",
            options.PipeName,
            PipeDirection.InOut,
            PipeOptions.Asynchronous);
        try
        {
            using CancellationTokenSource timeout = CancellationTokenSource.CreateLinkedTokenSource(
                cancellationToken);
            timeout.CancelAfter(options.HandshakeTimeout);
            await pipe.ConnectAsync(timeout.Token).ConfigureAwait(false);
            BrokerClient client = new(pipe, role, clientInstanceId, generation);
            BridgeEnvelope hello = new()
            {
                Version = BridgeFrameCodec.ProtocolVersion,
                Kind = BridgeMessageKind.Hello,
                Id = Guid.NewGuid().ToString("N"),
                Role = role,
                Method = "hello",
                Payload = BridgeJson.ToElement(new
                {
                    token = handshakeToken,
                    clientInstanceId,
                    processId = Environment.ProcessId,
                    executablePath = Environment.ProcessPath ?? string.Empty,
                    generation
                })
            };
            await client.SendAsync(hello, timeout.Token).ConfigureAwait(false);
            BridgeEnvelope? response = await BridgeFrameCodec.ReadAsync(
                pipe,
                timeout.Token).ConfigureAwait(false);
            if (response is null || response.Kind != BridgeMessageKind.Response || response.Ok != true)
            {
                string error = response?.Error ?? "The broker closed during handshake.";
                throw new BridgeProtocolException($"Bridge handshake rejected: {error}");
            }

            return client;
        }
        catch
        {
            await pipe.DisposeAsync().ConfigureAwait(false);
            throw;
        }
    }

    public async Task<BridgeResponse> RequestAsync(
        string method,
        JsonElement payload,
        CancellationToken cancellationToken)
    {
        long requestSequence = Interlocked.Increment(ref sequence);
        string requestId = Guid.NewGuid().ToString("N");
        return await RequestAsync(
            method,
            payload,
            requestId,
            requestSequence,
            cancellationToken).ConfigureAwait(false);
    }

    public async Task<BridgeResponse> RequestAsync(
        string method,
        JsonElement payload,
        string requestId,
        long requestSequence,
        CancellationToken cancellationToken)
    {
        await requestLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
        BridgeEnvelope request = new()
        {
            Version = BridgeFrameCodec.ProtocolVersion,
            Kind = BridgeMessageKind.Request,
            Id = requestId,
            Role = role,
            Method = method,
            Payload = BridgeJson.ToElement(new
            {
                sequence = requestSequence,
                data = payload
            })
        };
        await SendAsync(request, cancellationToken).ConfigureAwait(false);
        while (true)
        {
            BridgeEnvelope? response = await ReceiveAsync(cancellationToken).ConfigureAwait(false);
            if (response is null)
            {
                return BridgeResponse.Failure(requestId, "broker_disconnected");
            }

            if (response.Kind == BridgeMessageKind.Response && response.Id == requestId)
            {
                return response.Ok == true
                    ? BridgeResponse.Success(requestId, response.Payload)
                    : BridgeResponse.Failure(requestId, response.Error ?? "unknown_error");
            }
        }
        }
        finally
        {
            requestLock.Release();
        }
    }

    public ValueTask<BridgeEnvelope?> ReceiveAsync(CancellationToken cancellationToken)
    {
        return BridgeFrameCodec.ReadAsync(pipe, cancellationToken);
    }

    public Task SendResponseAsync(
        string requestId,
        JsonElement payload,
        CancellationToken cancellationToken)
    {
        BridgeEnvelope response = new()
        {
            Version = BridgeFrameCodec.ProtocolVersion,
            Kind = BridgeMessageKind.Response,
            Id = requestId,
            Role = role,
            Method = "response",
            Payload = payload,
            Ok = true
        };
        return SendAsync(response, cancellationToken);
    }

    public Task SendEventAsync(
        string method,
        JsonElement payload,
        string? eventId,
        CancellationToken cancellationToken)
    {
        BridgeEnvelope bridgeEvent = new()
        {
            Version = BridgeFrameCodec.ProtocolVersion,
            Kind = BridgeMessageKind.Event,
            Id = eventId ?? Guid.NewGuid().ToString("N"),
            Role = role,
            Method = method,
            Payload = payload
        };
        return SendAsync(bridgeEvent, cancellationToken);
    }

    public async ValueTask DisposeAsync()
    {
        requestLock.Dispose();
        writeLock.Dispose();
        await pipe.DisposeAsync().ConfigureAwait(false);
    }

    private async Task SendAsync(BridgeEnvelope envelope, CancellationToken cancellationToken)
    {
        await writeLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await BridgeFrameCodec.WriteAsync(pipe, envelope, cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            writeLock.Release();
        }
    }
}
