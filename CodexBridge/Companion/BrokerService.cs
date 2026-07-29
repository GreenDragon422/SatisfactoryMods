using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO.Pipes;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading.Channels;

namespace CodexBridge.Companion;

public sealed class BrokerService : IAsyncDisposable
{
    private readonly BrokerOptions options;
    private readonly IBridgeClientIdentityValidator identityValidator;
    private readonly CancellationTokenSource disposalCancellation = new();
    private readonly ConcurrentDictionary<Guid, Task> sessionTasks = new();
    private readonly RequestLedger requestLedger = new(maximumEntries: 256);
    private readonly BridgeDiagnosticLog diagnosticLog;
    private readonly ICodexConversationService conversationService;
    private readonly Channel<ChatWorkItem> chatQueue;
    private readonly object gameSynchronization = new();
    private readonly object pendingGameEventSynchronization = new();
    private readonly SemaphoreSlim pendingGameEventFlushGate = new(1, 1);
    private readonly Dictionary<string, GameGenerationEntry> gameGenerations = new(StringComparer.Ordinal);
    private readonly Queue<PendingGameEvent> pendingGameEvents = new();
    private GameBrokerSession? gameSession;
    private Semaphore? serviceSemaphore;
    private string? handshakeToken;
    private long lastActivityUtcTicks;
    private int pendingChatCount;

    public BrokerService(
        BrokerOptions options,
        IBridgeClientIdentityValidator identityValidator,
        BridgeDiagnosticLog? diagnosticLog = null,
        ICodexConversationService? conversationService = null)
    {
        this.options = options;
        this.identityValidator = identityValidator;
        this.diagnosticLog = diagnosticLog ?? new BridgeDiagnosticLog(options.StateDirectory);
        this.conversationService = conversationService ?? CreateConversationService(options);
        chatQueue = Channel.CreateBounded<ChatWorkItem>(new BoundedChannelOptions(8)
        {
            SingleReader = true,
            SingleWriter = false,
            FullMode = BoundedChannelFullMode.Wait
        });
        if (options.IdleTimeout <= TimeSpan.Zero || options.GameLeaseTimeout <= TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(options));
        }

        TouchActivity();
    }

    public TaskCompletionSource Started { get; } = new(
        TaskCreationOptions.RunContinuationsAsynchronously);

    public async Task RunAsync(CancellationToken cancellationToken)
    {
        serviceSemaphore = new Semaphore(1, 1, options.MutexName);
        if (!serviceSemaphore.WaitOne(0))
        {
            throw new InvalidOperationException("Another CodexBridge broker service is already running.");
        }

        handshakeToken = BridgeTokenStore.GetOrCreate(options.StateDirectory);
        diagnosticLog.Write("service_started");
        using CancellationTokenSource linkedCancellation = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken,
            disposalCancellation.Token);
        Task chatWorker = ProcessChatQueueAsync(linkedCancellation.Token);
        Started.TrySetResult();
        try
        {
            while (!linkedCancellation.IsCancellationRequested)
            {
                if (IsIdleExpired())
                {
                    diagnosticLog.Write("service_idle_shutdown");
                    break;
                }

                NamedPipeServerStream pipe = CreateServerPipe();
                using CancellationTokenSource acceptCancellation =
                    CancellationTokenSource.CreateLinkedTokenSource(linkedCancellation.Token);
                TimeSpan acceptPoll = TimeSpan.FromMilliseconds(Math.Min(
                    options.IdleTimeout.TotalMilliseconds,
                    1000));
                acceptCancellation.CancelAfter(acceptPoll);
                try
                {
                    await pipe.WaitForConnectionAsync(acceptCancellation.Token).ConfigureAwait(false);
                    TouchActivity();
                }
                catch (OperationCanceledException) when (!linkedCancellation.IsCancellationRequested)
                {
                    await pipe.DisposeAsync().ConfigureAwait(false);
                    continue;
                }
                catch
                {
                    await pipe.DisposeAsync().ConfigureAwait(false);
                    throw;
                }

                Guid sessionId = Guid.NewGuid();
                Task sessionTask = HandleClientAsync(pipe, linkedCancellation.Token);
                sessionTasks.TryAdd(sessionId, sessionTask);
                _ = sessionTask.ContinueWith(
                    _ =>
                    {
                        sessionTasks.TryRemove(sessionId, out Task? _);
                        TouchActivity();
                    },
                    CancellationToken.None,
                    TaskContinuationOptions.ExecuteSynchronously,
                    TaskScheduler.Default);
            }
        }
        catch (OperationCanceledException) when (linkedCancellation.IsCancellationRequested)
        {
        }
        finally
        {
            chatQueue.Writer.TryComplete();
            Task[] remainingSessions = sessionTasks.Values.ToArray();
            if (remainingSessions.Length > 0)
            {
                await Task.WhenAll(remainingSessions).ConfigureAwait(false);
            }

            try
            {
                await chatWorker.ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (linkedCancellation.IsCancellationRequested)
            {
            }

            serviceSemaphore.Release();
            serviceSemaphore.Dispose();
            serviceSemaphore = null;
            diagnosticLog.Write("service_stopped");
        }
    }

    public async ValueTask DisposeAsync()
    {
        disposalCancellation.Cancel();
        GameBrokerSession? session;
        lock (gameSynchronization)
        {
            session = gameSession;
            gameSession = null;
        }

        if (session is not null)
        {
            await session.DisposeAsync().ConfigureAwait(false);
        }

        disposalCancellation.Dispose();
    }

    private NamedPipeServerStream CreateServerPipe()
    {
        if (!OperatingSystem.IsWindows())
        {
            throw new PlatformNotSupportedException("CodexBridge named pipes require Windows.");
        }

        return SecureNamedPipeServerFactory.Create(options.PipeName);
    }

    private async Task HandleClientAsync(
        NamedPipeServerStream pipe,
        CancellationToken cancellationToken)
    {
        await using (pipe.ConfigureAwait(false))
        {
            string? connectionRole = null;
            string? connectionClientInstanceId = null;
            try
            {
                using CancellationTokenSource handshakeCancellation =
                    CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
                handshakeCancellation.CancelAfter(options.HandshakeTimeout);
                BridgeEnvelope? hello = await BridgeFrameCodec.ReadAsync(
                    pipe,
                    handshakeCancellation.Token).ConfigureAwait(false);
                if (!TryValidateHello(pipe, hello, out ClientHello? clientHello, out string error))
                {
                    diagnosticLog.Write(
                        "connection_rejected",
                        role: hello?.Role,
                        correlationId: hello?.Id,
                        reason: error);
                    await SendHandshakeResponseAsync(
                        pipe,
                        hello?.Id ?? Guid.NewGuid().ToString("N"),
                        ok: false,
                        error,
                        handshakeCancellation.Token).ConfigureAwait(false);
                    return;
                }

                connectionRole = clientHello!.Role;
                connectionClientInstanceId = clientHello.ClientInstanceId;

                GameBrokerSession? acceptedGameSession = null;
                if (clientHello.Role == "game" &&
                    !TryRegisterGameSession(
                        pipe,
                        clientHello,
                        out acceptedGameSession,
                        out error))
                {
                    diagnosticLog.Write(
                        "connection_rejected",
                        role: clientHello.Role,
                        clientInstanceId: clientHello.ClientInstanceId,
                        correlationId: hello!.Id,
                        reason: error);
                    await SendHandshakeResponseAsync(
                        pipe,
                        hello!.Id,
                        ok: false,
                        error,
                        handshakeCancellation.Token).ConfigureAwait(false);
                    return;
                }

                diagnosticLog.Write(
                    "connection_accepted",
                    role: clientHello.Role,
                    clientInstanceId: clientHello.ClientInstanceId,
                    correlationId: hello!.Id);

                await SendHandshakeResponseAsync(
                    pipe,
                    hello!.Id,
                    ok: true,
                    error: null,
                    handshakeCancellation.Token).ConfigureAwait(false);
                if (acceptedGameSession is not null)
                {
                    Task gameSessionTask = RunGameSessionAsync(
                        acceptedGameSession,
                        cancellationToken);
                    await FlushPendingGameEventsAsync(
                        acceptedGameSession,
                        cancellationToken).ConfigureAwait(false);
                    await gameSessionTask.ConfigureAwait(false);
                    return;
                }

                await RunMcpSessionAsync(pipe, clientHello, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                diagnosticLog.Write(
                    "connection_error",
                    role: connectionRole,
                    clientInstanceId: connectionClientInstanceId,
                    reason: "operation_canceled");
            }
            catch (IOException)
            {
                diagnosticLog.Write(
                    "connection_error",
                    role: connectionRole,
                    clientInstanceId: connectionClientInstanceId,
                    reason: "io_error");
            }
            catch (ObjectDisposedException)
            {
                diagnosticLog.Write(
                    "connection_error",
                    role: connectionRole,
                    clientInstanceId: connectionClientInstanceId,
                    reason: "connection_disposed");
            }
            catch (BridgeProtocolException)
            {
                diagnosticLog.Write(
                    "connection_error",
                    role: connectionRole,
                    clientInstanceId: connectionClientInstanceId,
                    reason: "protocol_error");
            }
            finally
            {
                diagnosticLog.Write(
                    "connection_closed",
                    role: connectionRole,
                    clientInstanceId: connectionClientInstanceId);
            }
        }
    }

    private bool TryValidateHello(
        NamedPipeServerStream pipe,
        BridgeEnvelope? envelope,
        out ClientHello? clientHello,
        out string error)
    {
        clientHello = null;
        error = "handshake_invalid";
        if (envelope is null || envelope.Kind != BridgeMessageKind.Hello || envelope.Method != "hello")
        {
            return false;
        }

        try
        {
            JsonElement payload = envelope.Payload;
            string token = payload.GetProperty("token").GetString() ?? string.Empty;
            string clientInstanceId = payload.GetProperty("clientInstanceId").GetString() ?? string.Empty;
            int processId = payload.GetProperty("processId").GetInt32();
            string executablePath = payload.GetProperty("executablePath").GetString() ?? string.Empty;
            long generation = payload.GetProperty("generation").GetInt64();
            if (string.IsNullOrWhiteSpace(clientInstanceId) || clientInstanceId.Length > 128 ||
                executablePath.Length > 32768 ||
                (envelope.Role == "mcp" && generation != 0) ||
                (envelope.Role == "game" && generation <= 0))
            {
                error = "handshake_identity_invalid";
                return false;
            }

            if (!TokenEquals(handshakeToken!, token))
            {
                error = "handshake_token_rejected";
                return false;
            }

            int kernelProcessId = NamedPipeProcessInspector.GetClientProcessId(pipe.SafePipeHandle);
            if (kernelProcessId != processId ||
                !identityValidator.IsAllowed(envelope.Role, processId, executablePath))
            {
                error = "handshake_identity_rejected";
                return false;
            }

            clientHello = new ClientHello(
                envelope.Role,
                clientInstanceId,
                processId,
                executablePath,
                generation);
            return true;
        }
        catch (InvalidOperationException)
        {
            return false;
        }
        catch (KeyNotFoundException)
        {
            return false;
        }
    }

    private async Task RunGameSessionAsync(
        GameBrokerSession session,
        CancellationToken cancellationToken)
    {
        try
        {
            await session.RunAsync(cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            lock (gameSynchronization)
            {
                if (ReferenceEquals(gameSession, session))
                {
                    gameSession = null;
                }
            }

            diagnosticLog.Write(
                "game_connection_closed",
                role: "game",
                clientInstanceId: session.ClientInstanceId);
        }
    }

    private bool TryRegisterGameSession(
        NamedPipeServerStream pipe,
        ClientHello hello,
        out GameBrokerSession? acceptedSession,
        out string error)
    {
        acceptedSession = null;
        error = "game_lease_rejected";
        GameBrokerSession? replacedSession = null;
        lock (gameSynchronization)
        {
            RemoveExpiredGenerationEntries();
            if (gameGenerations.TryGetValue(
                    hello.ClientInstanceId,
                    out GameGenerationEntry? generationEntry) &&
                hello.Generation <= generationEntry.Generation)
            {
                error = "game_generation_stale";
                return false;
            }

            GameBrokerSession? activeSession = gameSession;
            if (activeSession is not null &&
                !string.Equals(
                    activeSession.ClientInstanceId,
                    hello.ClientInstanceId,
                    StringComparison.Ordinal) &&
                !activeSession.IsLeaseExpired(options.GameLeaseTimeout) &&
                IsProcessAlive(activeSession.ProcessId))
            {
                return false;
            }

            if (!gameGenerations.ContainsKey(hello.ClientInstanceId) && gameGenerations.Count >= 1024)
            {
                string? oldestInactiveKey = gameGenerations
                    .Where(pair => gameSession is null ||
                        !string.Equals(
                            pair.Key,
                            gameSession.ClientInstanceId,
                            StringComparison.Ordinal))
                    .OrderBy(pair => pair.Value.LastSeenUtc)
                    .Select(pair => pair.Key)
                    .FirstOrDefault();
                if (oldestInactiveKey is null)
                {
                    error = "game_lease_table_busy";
                    return false;
                }

                gameGenerations.Remove(oldestInactiveKey);
            }

            acceptedSession = new GameBrokerSession(
                pipe,
                hello.ClientInstanceId,
                hello.ProcessId,
                hello.Generation,
                options.RequestTimeout,
                HandleGameEventAsync);
            replacedSession = gameSession;
            gameSession = acceptedSession;
            gameGenerations[hello.ClientInstanceId] = new GameGenerationEntry(
                hello.Generation,
                DateTimeOffset.UtcNow);
        }

        replacedSession?.Disconnect();
        diagnosticLog.Write(
            "game_lease_acquired",
            role: "game",
            clientInstanceId: hello.ClientInstanceId);
        return true;
    }

    private static bool IsProcessAlive(int processId)
    {
        try
        {
            using System.Diagnostics.Process process = System.Diagnostics.Process.GetProcessById(processId);
            return !process.HasExited;
        }
        catch (ArgumentException)
        {
            return false;
        }
        catch (InvalidOperationException)
        {
            return false;
        }
        catch (System.ComponentModel.Win32Exception)
        {
            return false;
        }
    }

    private void RemoveExpiredGenerationEntries()
    {
        DateTimeOffset cutoff = DateTimeOffset.UtcNow.Subtract(TimeSpan.FromMinutes(30));
        string[] expiredKeys = gameGenerations
            .Where(pair => pair.Value.LastSeenUtc < cutoff &&
                (gameSession is null ||
                 !string.Equals(pair.Key, gameSession.ClientInstanceId, StringComparison.Ordinal)))
            .Select(pair => pair.Key)
            .ToArray();
        foreach (string expiredKey in expiredKeys)
        {
            gameGenerations.Remove(expiredKey);
        }
    }

    private async Task RunMcpSessionAsync(
        NamedPipeServerStream pipe,
        ClientHello hello,
        CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested && pipe.IsConnected)
        {
            BridgeEnvelope? request = await BridgeFrameCodec.ReadAsync(
                pipe,
                cancellationToken).ConfigureAwait(false);
            if (request is null)
            {
                return;
            }

            if (request.Kind != BridgeMessageKind.Request)
            {
                continue;
            }

            Stopwatch stopwatch = Stopwatch.StartNew();
            JsonElement diagnosticPayload = request.Payload.TryGetProperty(
                "data",
                out JsonElement diagnosticData)
                ? diagnosticData
                : BridgeJson.EmptyObject;
            diagnosticLog.Write(
                "request_started",
                role: hello.Role,
                clientInstanceId: hello.ClientInstanceId,
                correlationId: request.Id,
                method: request.Method,
                requestPayload: diagnosticPayload);
            BridgeResponse response = await HandleMcpRequestAsync(
                hello,
                request,
                cancellationToken).ConfigureAwait(false);
            stopwatch.Stop();
            diagnosticLog.Write(
                "request_completed",
                role: hello.Role,
                clientInstanceId: hello.ClientInstanceId,
                correlationId: request.Id,
                method: request.Method,
                reason: response.Error,
                durationMilliseconds: stopwatch.ElapsedMilliseconds,
                requestPayload: diagnosticPayload);
            BridgeEnvelope responseEnvelope = new()
            {
                Version = BridgeFrameCodec.ProtocolVersion,
                Kind = BridgeMessageKind.Response,
                Id = request.Id,
                Role = "service",
                Method = request.Method,
                Payload = response.Payload,
                Ok = response.Ok,
                Error = response.Error
            };
            await BridgeFrameCodec.WriteAsync(
                pipe,
                responseEnvelope,
                cancellationToken).ConfigureAwait(false);
        }
    }

    private async Task<BridgeResponse> HandleMcpRequestAsync(
        ClientHello hello,
        BridgeEnvelope request,
        CancellationToken cancellationToken)
    {
        if (request.Method == "service.status")
        {
            GameBrokerSession? statusSession = GetGameSession();
            return BridgeResponse.Success(request.Id, BridgeJson.ToElement(new
            {
                gameOnline = statusSession is not null,
                protocolVersion = BridgeFrameCodec.ProtocolVersion,
                gameGeneration = statusSession?.Generation
            }));
        }

        if (request.Method == "bridge.conversation")
        {
            JsonElement conversationData = request.Payload.TryGetProperty(
                "data",
                out JsonElement conversationRequestData)
                ? conversationRequestData
                : BridgeJson.EmptyObject;
            bool reset = conversationData.TryGetProperty("reset", out JsonElement resetElement) &&
                resetElement.ValueKind == JsonValueKind.True;
            CodexConversationStatus conversationStatus = reset
                ? await conversationService.ResetAsync(cancellationToken).ConfigureAwait(false)
                : await conversationService.GetStatusAsync(cancellationToken).ConfigureAwait(false);
            return BridgeResponse.Success(request.Id, BridgeJson.ToElement(new
            {
                conversationStatus.HasThread,
                conversationStatus.ThreadId,
                conversationStatus.WorkspaceDirectory,
                pending = Volatile.Read(ref pendingChatCount)
            }));
        }

        GameBrokerSession? session = GetGameSession();
        if (session is null)
        {
            return BridgeResponse.Failure(request.Id, "game_offline");
        }

        if (!request.Payload.TryGetProperty("sequence", out JsonElement sequenceElement) ||
            !sequenceElement.TryGetInt64(out long sequence) ||
            sequence <= 0)
        {
            return BridgeResponse.Failure(request.Id, "request_sequence_invalid");
        }

        JsonElement data = request.Payload.TryGetProperty("data", out JsonElement requestData)
            ? requestData
            : BridgeJson.EmptyObject;
        RequestIdentity identity = new(
            hello.ClientInstanceId,
            sequence,
            request.Id,
            session.Generation);
        BridgeRequestEffect effect = ClassifyEffect(request.Method);
        RequestStartResult start = requestLedger.TryBegin(identity, effect);
        switch (start.Disposition)
        {
            case RequestStartDisposition.InProgress:
                return BridgeResponse.Failure(request.Id, "request_in_progress");

            case RequestStartDisposition.ReplayCompleted:
                return start.CachedResponse ??
                    BridgeResponse.Failure(request.Id, "cached_response_missing");

            case RequestStartDisposition.DeliveryUnknown:
                return BridgeResponse.Failure(request.Id, "delivery_unknown");

            case RequestStartDisposition.Busy:
                return BridgeResponse.Failure(request.Id, "busy");

            case RequestStartDisposition.StaleSequence:
                return BridgeResponse.Failure(request.Id, "request_sequence_stale");

            case RequestStartDisposition.Accepted:
                break;

            default:
                throw new InvalidOperationException(
                    $"Unsupported request disposition {start.Disposition}.");
        }

        try
        {
            BridgeResponse response = await session.InvokeAsync(
                request.Id,
                request.Method,
                data,
                cancellationToken).ConfigureAwait(false);
            if (response.Error is "delivery_unknown" or "game_timeout")
            {
                requestLedger.MarkConnectionLost(identity);
            }
            else
            {
                requestLedger.MarkCompleted(identity, response);
            }

            return response;
        }
        catch (IOException)
        {
            requestLedger.MarkConnectionLost(identity);
            return BridgeResponse.Failure(request.Id, "delivery_unknown");
        }
        catch (ObjectDisposedException)
        {
            requestLedger.MarkConnectionLost(identity);
            return BridgeResponse.Failure(request.Id, "delivery_unknown");
        }
    }

    private GameBrokerSession? GetGameSession()
    {
        lock (gameSynchronization)
        {
            if (gameSession is not null && gameSession.IsLeaseExpired(options.GameLeaseTimeout))
            {
                GameBrokerSession expiredSession = gameSession;
                gameSession = null;
                expiredSession.Disconnect();
                diagnosticLog.Write(
                    "game_lease_expired",
                    role: "game",
                    clientInstanceId: expiredSession.ClientInstanceId);
            }

            return gameSession;
        }
    }

    private async Task HandleGameEventAsync(
        GameBrokerSession sourceSession,
        BridgeEnvelope bridgeEvent,
        CancellationToken cancellationToken)
    {
        lock (gameSynchronization)
        {
            if (!ReferenceEquals(gameSession, sourceSession))
            {
                return;
            }
        }

        string? message = null;
        if (bridgeEvent.Method == "chat.request")
        {
            message = bridgeEvent.Payload.TryGetProperty(
                "message",
                out JsonElement messageElement)
                ? messageElement.GetString()?.Trim()
                : null;
            if (string.IsNullOrWhiteSpace(message) || message.Length > 8192)
            {
                await sourceSession.SendEventAsync(
                    "chat.error",
                    BridgeJson.ToElement(new
                    {
                        correlationId = bridgeEvent.Id,
                        error = "message_invalid"
                    }),
                    cancellationToken).ConfigureAwait(false);
                return;
            }
        }
        else if (bridgeEvent.Method != "chat.new_thread")
        {
            await sourceSession.SendEventAsync(
                "chat.error",
                BridgeJson.ToElement(new
                {
                    correlationId = bridgeEvent.Id,
                    error = "chat_event_unknown"
                }),
                cancellationToken).ConfigureAwait(false);
            return;
        }

        TaskCompletionSource acknowledged = new(
            TaskCreationOptions.RunContinuationsAsynchronously);
        ChatWorkItem workItem = new(
            bridgeEvent.Id,
            bridgeEvent.Method,
            message,
            acknowledged);
        Interlocked.Increment(ref pendingChatCount);
        if (!chatQueue.Writer.TryWrite(workItem))
        {
            Interlocked.Decrement(ref pendingChatCount);
            await sourceSession.SendEventAsync(
                "chat.error",
                BridgeJson.ToElement(new
                {
                    correlationId = bridgeEvent.Id,
                    error = "chat_queue_busy"
                }),
                cancellationToken).ConfigureAwait(false);
            return;
        }

        diagnosticLog.Write(
            "chat_queued",
            role: "game",
            clientInstanceId: sourceSession.ClientInstanceId,
            correlationId: bridgeEvent.Id,
            method: bridgeEvent.Method);
        try
        {
            await sourceSession.SendEventAsync(
                "chat.accepted",
                BridgeJson.ToElement(new
                {
                    correlationId = bridgeEvent.Id,
                    pending = Volatile.Read(ref pendingChatCount)
                }),
                cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            acknowledged.TrySetResult();
        }
    }

    private async Task ProcessChatQueueAsync(CancellationToken cancellationToken)
    {
        await foreach (ChatWorkItem workItem in chatQueue.Reader.ReadAllAsync(cancellationToken))
        {
            await workItem.Acknowledged.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
            Stopwatch stopwatch = Stopwatch.StartNew();
            try
            {
                if (workItem.Method == "chat.new_thread")
                {
                    CodexConversationStatus status = await conversationService.ResetAsync(
                        cancellationToken).ConfigureAwait(false);
                    await DeliverGameEventAsync(
                        "chat.thread_reset",
                        BridgeJson.ToElement(new
                        {
                            correlationId = workItem.CorrelationId,
                            status.HasThread,
                            status.ThreadId
                        }),
                        cancellationToken).ConfigureAwait(false);
                }
                else
                {
                    CodexConversationResult result = await conversationService.SendAsync(
                        workItem.Message!,
                        cancellationToken).ConfigureAwait(false);
                    if (result.Ok)
                    {
                        await DeliverGameEventAsync(
                            "chat.reply",
                            BridgeJson.ToElement(new
                            {
                                correlationId = workItem.CorrelationId,
                                message = result.Reply,
                                result.ThreadId
                            }),
                            cancellationToken).ConfigureAwait(false);
                    }
                    else
                    {
                        await DeliverGameEventAsync(
                            "chat.error",
                            BridgeJson.ToElement(new
                            {
                                correlationId = workItem.CorrelationId,
                                error = result.Error ?? "codex_turn_failed",
                                result.ThreadId
                            }),
                            cancellationToken).ConfigureAwait(false);
                    }
                }
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                throw;
            }
            catch (Exception exception) when (
                exception is IOException or UnauthorizedAccessException or InvalidOperationException)
            {
                await DeliverGameEventAsync(
                    "chat.error",
                    BridgeJson.ToElement(new
                    {
                        correlationId = workItem.CorrelationId,
                        error = "conversation_internal_error"
                    }),
                    cancellationToken).ConfigureAwait(false);
            }
            finally
            {
                stopwatch.Stop();
                Interlocked.Decrement(ref pendingChatCount);
                diagnosticLog.Write(
                    "chat_completed",
                    role: "service",
                    correlationId: workItem.CorrelationId,
                    method: workItem.Method,
                    durationMilliseconds: stopwatch.ElapsedMilliseconds);
            }
        }
    }

    private async Task DeliverGameEventAsync(
        string method,
        JsonElement payload,
        CancellationToken cancellationToken)
    {
        PendingGameEvent pendingEvent = new(
            Guid.NewGuid().ToString("N"),
            method,
            payload.Clone());
        if (!QueuePendingGameEvent(pendingEvent))
        {
            diagnosticLog.Write(
                "game_event_queue_full",
                role: "service",
                correlationId: pendingEvent.DeliveryId,
                method: method);
            return;
        }

        GameBrokerSession? session = GetGameSession();
        if (session is null)
        {
            return;
        }

        await FlushPendingGameEventsAsync(session, cancellationToken).ConfigureAwait(false);
    }

    private bool QueuePendingGameEvent(PendingGameEvent pendingEvent)
    {
        lock (pendingGameEventSynchronization)
        {
            if (pendingGameEvents.Count >= 16)
            {
                return false;
            }

            pendingGameEvents.Enqueue(pendingEvent);
            return true;
        }
    }

    private async Task FlushPendingGameEventsAsync(
        GameBrokerSession session,
        CancellationToken cancellationToken)
    {
        await pendingGameEventFlushGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            while (true)
            {
                PendingGameEvent? pendingEvent;
                lock (pendingGameEventSynchronization)
                {
                    pendingEvent = pendingGameEvents.Count > 0
                        ? pendingGameEvents.Peek()
                        : null;
                }

                if (pendingEvent is null)
                {
                    return;
                }

                BridgeResponse response;
                try
                {
                    response = await session.InvokeAsync(
                        pendingEvent.DeliveryId,
                        "chat.deliver",
                        BridgeJson.ToElement(new
                        {
                            eventMethod = pendingEvent.Method,
                            eventPayload = pendingEvent.Payload
                        }),
                        cancellationToken).ConfigureAwait(false);
                }
                catch (Exception exception) when (
                    exception is IOException or ObjectDisposedException)
                {
                    return;
                }

                if (!response.Ok)
                {
                    session.Disconnect();
                    return;
                }

                lock (pendingGameEventSynchronization)
                {
                    if (pendingGameEvents.Count > 0 &&
                        pendingGameEvents.Peek().DeliveryId == pendingEvent.DeliveryId)
                    {
                        pendingGameEvents.Dequeue();
                    }
                }

                diagnosticLog.Write(
                    "game_event_delivered",
                    role: "game",
                    clientInstanceId: session.ClientInstanceId,
                    correlationId: pendingEvent.DeliveryId,
                    method: pendingEvent.Method);
            }
        }
        finally
        {
            pendingGameEventFlushGate.Release();
        }
    }

    private static ICodexConversationService CreateConversationService(BrokerOptions options)
    {
        BridgeRuntimeSettings settings = BridgeRuntimeSettings.Load();
        return new CodexConversationService(
            new FileConversationStateStore(options.StateDirectory),
            new CodexCliRunner(),
            settings.WorkspaceDirectory);
    }

    private bool IsIdleExpired()
    {
        if (!sessionTasks.IsEmpty || Volatile.Read(ref pendingChatCount) > 0)
        {
            return false;
        }

        DateTimeOffset lastActivity = new(Interlocked.Read(ref lastActivityUtcTicks), TimeSpan.Zero);
        return DateTimeOffset.UtcNow - lastActivity >= options.IdleTimeout;
    }

    private void TouchActivity()
    {
        Interlocked.Exchange(ref lastActivityUtcTicks, DateTimeOffset.UtcNow.UtcTicks);
    }

    private static BridgeRequestEffect ClassifyEffect(string method)
    {
        return method switch
        {
            "game.status" => BridgeRequestEffect.ReadOnly,
            "console.discover" => BridgeRequestEffect.ReadOnly,
            "log.recent" => BridgeRequestEffect.ReadOnly,
            "result.read" => BridgeRequestEffect.ReadOnly,
            "bridge.conversation" => BridgeRequestEffect.NonDestructive,
            "game.message" => BridgeRequestEffect.NonDestructive,
            "chat.send" => BridgeRequestEffect.NonDestructive,
            "console.execute" => BridgeRequestEffect.PotentiallyDestructive,
            _ => BridgeRequestEffect.PotentiallyDestructive
        };
    }

    private static async Task SendHandshakeResponseAsync(
        Stream pipe,
        string requestId,
        bool ok,
        string? error,
        CancellationToken cancellationToken)
    {
        BridgeEnvelope response = new()
        {
            Version = BridgeFrameCodec.ProtocolVersion,
            Kind = BridgeMessageKind.Response,
            Id = requestId,
            Role = "service",
            Method = "hello",
            Payload = BridgeJson.EmptyObject,
            Ok = ok,
            Error = error
        };
        await BridgeFrameCodec.WriteAsync(pipe, response, cancellationToken).ConfigureAwait(false);
    }

    private static bool TokenEquals(string expected, string actual)
    {
        byte[] expectedBytes = Encoding.UTF8.GetBytes(expected);
        byte[] actualBytes = Encoding.UTF8.GetBytes(actual);
        return expectedBytes.Length == actualBytes.Length &&
            CryptographicOperations.FixedTimeEquals(expectedBytes, actualBytes);
    }

    private sealed record ClientHello(
        string Role,
        string ClientInstanceId,
        int ProcessId,
        string ExecutablePath,
        long Generation);

    private sealed record GameGenerationEntry(long Generation, DateTimeOffset LastSeenUtc);

    private sealed record ChatWorkItem(
        string CorrelationId,
        string Method,
        string? Message,
        TaskCompletionSource Acknowledged);

    private sealed record PendingGameEvent(
        string DeliveryId,
        string Method,
        JsonElement Payload);
}

internal sealed class GameBrokerSession : IAsyncDisposable
{
    private const int EventQueueCapacity = 64;

    private readonly Stream pipe;
    private readonly TimeSpan requestTimeout;
    private readonly SemaphoreSlim writeLock = new(1, 1);
    private readonly ConcurrentDictionary<string, TaskCompletionSource<BridgeResponse>> pending = new();
    private readonly Func<GameBrokerSession, BridgeEnvelope, CancellationToken, Task> eventHandler;
    private readonly Channel<BridgeEnvelope> eventQueue = Channel.CreateBounded<BridgeEnvelope>(
        new BoundedChannelOptions(EventQueueCapacity)
        {
            SingleReader = true,
            SingleWriter = true,
            FullMode = BoundedChannelFullMode.Wait
        });

    private long lastActivityTimestamp;

    public GameBrokerSession(
        Stream pipe,
        string clientInstanceId,
        int processId,
        long generation,
        TimeSpan requestTimeout,
        Func<GameBrokerSession, BridgeEnvelope, CancellationToken, Task> eventHandler)
    {
        this.pipe = pipe;
        ClientInstanceId = clientInstanceId;
        ProcessId = processId;
        Generation = generation;
        this.requestTimeout = requestTimeout;
        this.eventHandler = eventHandler;
        Touch();
    }

    public string ClientInstanceId { get; }

    public int ProcessId { get; }

    public long Generation { get; }

    public bool IsLeaseExpired(TimeSpan leaseTimeout)
    {
        long elapsed = TimeProvider.System.GetElapsedTime(
            Interlocked.Read(ref lastActivityTimestamp),
            TimeProvider.System.GetTimestamp()).Ticks;
        return elapsed >= leaseTimeout.Ticks;
    }

    public async Task RunAsync(CancellationToken cancellationToken)
    {
        Task eventProcessor = ProcessEventsAsync(cancellationToken);
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                BridgeEnvelope? envelope = await BridgeFrameCodec.ReadAsync(
                    pipe,
                    cancellationToken).ConfigureAwait(false);
                if (envelope is null)
                {
                    break;
                }

                Touch();

                if (envelope.Kind == BridgeMessageKind.Response &&
                    pending.TryRemove(envelope.Id, out TaskCompletionSource<BridgeResponse>? completion))
                {
                    BridgeResponse response = envelope.Ok == true
                        ? BridgeResponse.Success(envelope.Id, envelope.Payload)
                        : BridgeResponse.Failure(envelope.Id, envelope.Error ?? "game_error");
                    completion.TrySetResult(response);
                }
                else if (envelope.Kind == BridgeMessageKind.Event)
                {
                    await eventQueue.Writer.WriteAsync(
                        envelope,
                        cancellationToken).ConfigureAwait(false);
                }
            }
        }
        finally
        {
            eventQueue.Writer.TryComplete();
            try
            {
                await eventProcessor.ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
            }
            catch (Exception exception) when (
                exception is IOException or ObjectDisposedException)
            {
            }

            foreach (TaskCompletionSource<BridgeResponse> completion in pending.Values)
            {
                completion.TrySetResult(BridgeResponse.Failure("unknown", "delivery_unknown"));
            }

            pending.Clear();
        }
    }

    private async Task ProcessEventsAsync(CancellationToken cancellationToken)
    {
        await foreach (BridgeEnvelope bridgeEvent in eventQueue.Reader.ReadAllAsync(
            cancellationToken))
        {
            await eventHandler(this, bridgeEvent, cancellationToken).ConfigureAwait(false);
        }
    }

    public async Task<BridgeResponse> InvokeAsync(
        string requestId,
        string method,
        JsonElement payload,
        CancellationToken cancellationToken)
    {
        TaskCompletionSource<BridgeResponse> completion = new(
            TaskCreationOptions.RunContinuationsAsynchronously);
        if (!pending.TryAdd(requestId, completion))
        {
            return BridgeResponse.Failure(requestId, "duplicate_request");
        }

        BridgeEnvelope request = new()
        {
            Version = BridgeFrameCodec.ProtocolVersion,
            Kind = BridgeMessageKind.Request,
            Id = requestId,
            Role = "service",
            Method = method,
            Payload = payload
        };
        await writeLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await BridgeFrameCodec.WriteAsync(pipe, request, cancellationToken).ConfigureAwait(false);
            Touch();
        }
        catch
        {
            pending.TryRemove(requestId, out TaskCompletionSource<BridgeResponse>? _);
            throw;
        }
        finally
        {
            writeLock.Release();
        }

        using CancellationTokenSource timeout = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken);
        timeout.CancelAfter(requestTimeout);
        try
        {
            return await completion.Task.WaitAsync(timeout.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            pending.TryRemove(requestId, out TaskCompletionSource<BridgeResponse>? _);
            return BridgeResponse.Failure(requestId, "game_timeout");
        }
    }

    public async Task SendEventAsync(
        string method,
        JsonElement payload,
        CancellationToken cancellationToken)
    {
        BridgeEnvelope bridgeEvent = new()
        {
            Version = BridgeFrameCodec.ProtocolVersion,
            Kind = BridgeMessageKind.Event,
            Id = Guid.NewGuid().ToString("N"),
            Role = "service",
            Method = method,
            Payload = payload
        };
        await writeLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await BridgeFrameCodec.WriteAsync(
                pipe,
                bridgeEvent,
                cancellationToken).ConfigureAwait(false);
            Touch();
        }
        finally
        {
            writeLock.Release();
        }
    }

    public ValueTask DisposeAsync()
    {
        writeLock.Dispose();
        return ValueTask.CompletedTask;
    }

    public void Disconnect()
    {
        pipe.Dispose();
    }

    private void Touch()
    {
        Interlocked.Exchange(ref lastActivityTimestamp, TimeProvider.System.GetTimestamp());
    }
}
