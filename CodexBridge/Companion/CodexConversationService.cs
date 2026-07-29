namespace CodexBridge.Companion;

public interface IConversationStateStore
{
    string? ReadThreadId();

    void WriteThreadId(string threadId);

    void Clear();
}

public interface ICodexCliRunner
{
    Task<CodexCliTurnResult> RunAsync(
        string? threadId,
        string prompt,
        string workspaceDirectory,
        CancellationToken cancellationToken);
}

public interface ICodexConversationService
{
    Task<CodexConversationResult> SendAsync(
        string message,
        CancellationToken cancellationToken);

    Task<CodexConversationStatus> GetStatusAsync(CancellationToken cancellationToken);

    Task<CodexConversationStatus> ResetAsync(CancellationToken cancellationToken);
}

public sealed record CodexCliTurnResult(
    bool Ok,
    string? ThreadId,
    string? Reply,
    string? Error,
    bool ThreadNotFound);

public sealed record CodexConversationResult(
    bool Ok,
    string? ThreadId,
    string? Reply,
    string? Error);

public sealed record CodexConversationStatus(
    bool HasThread,
    string? ThreadId,
    string? WorkspaceDirectory);

public sealed class CodexConversationService : ICodexConversationService
{
    private const int MaximumMessageLength = 8192;
    private const int MaximumReplyLength = 8000;

    private readonly IConversationStateStore stateStore;
    private readonly ICodexCliRunner runner;
    private readonly string? workspaceDirectory;
    private readonly SemaphoreSlim conversationGate = new(1, 1);
    private readonly object stateSynchronization = new();
    private string? threadIdSnapshot;

    public CodexConversationService(
        IConversationStateStore stateStore,
        ICodexCliRunner runner,
        string? workspaceDirectory)
    {
        this.stateStore = stateStore;
        this.runner = runner;
        this.workspaceDirectory = workspaceDirectory;
        threadIdSnapshot = stateStore.ReadThreadId();
    }

    public async Task<CodexConversationResult> SendAsync(
        string message,
        CancellationToken cancellationToken)
    {
        string normalizedMessage = message.Trim();
        if (normalizedMessage.Length is 0 or > MaximumMessageLength)
        {
            return new CodexConversationResult(false, null, null, "message_invalid");
        }

        if (string.IsNullOrWhiteSpace(workspaceDirectory))
        {
            return new CodexConversationResult(false, null, null, "workspace_not_configured");
        }

        await conversationGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            string? threadId = GetThreadIdSnapshot();
            string prompt = BuildPrompt(normalizedMessage);
            CodexCliTurnResult turn = await runner.RunAsync(
                threadId,
                prompt,
                workspaceDirectory,
                cancellationToken).ConfigureAwait(false);
            if (!turn.Ok && threadId is not null && turn.ThreadNotFound)
            {
                stateStore.Clear();
                SetThreadIdSnapshot(null);
                turn = await runner.RunAsync(
                    null,
                    prompt,
                    workspaceDirectory,
                    cancellationToken).ConfigureAwait(false);
            }

            if (!turn.Ok)
            {
                return new CodexConversationResult(
                    false,
                    threadId,
                    null,
                    turn.Error ?? "codex_turn_failed");
            }

            if (turn.ThreadId is null || !Guid.TryParse(turn.ThreadId, out Guid _))
            {
                return new CodexConversationResult(false, threadId, null, "codex_thread_invalid");
            }

            string reply = turn.Reply?.Trim() ?? string.Empty;
            if (reply.Length == 0)
            {
                return new CodexConversationResult(false, turn.ThreadId, null, "codex_reply_empty");
            }

            if (reply.Length > MaximumReplyLength)
            {
                const string suffix = "\n\n[Reply truncated in game; the complete reply remains in the Codex task.]";
                reply = reply[..(MaximumReplyLength - suffix.Length)] + suffix;
            }

            stateStore.WriteThreadId(turn.ThreadId);
            SetThreadIdSnapshot(turn.ThreadId);
            return new CodexConversationResult(true, turn.ThreadId, reply, null);
        }
        finally
        {
            conversationGate.Release();
        }
    }

    public Task<CodexConversationStatus> GetStatusAsync(
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        string? threadId = GetThreadIdSnapshot();
        return Task.FromResult(new CodexConversationStatus(
            threadId is not null,
            threadId,
            workspaceDirectory));
    }

    public async Task<CodexConversationStatus> ResetAsync(
        CancellationToken cancellationToken)
    {
        await conversationGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            stateStore.Clear();
            SetThreadIdSnapshot(null);
            return new CodexConversationStatus(false, null, workspaceDirectory);
        }
        finally
        {
            conversationGate.Release();
        }
    }

    private string? GetThreadIdSnapshot()
    {
        lock (stateSynchronization)
        {
            return threadIdSnapshot;
        }
    }

    private void SetThreadIdSnapshot(string? threadId)
    {
        lock (stateSynchronization)
        {
            threadIdSnapshot = threadId;
        }
    }

    private static string BuildPrompt(string message)
    {
        return "This user message came from the Satisfactory game through the dedicated CodexBridge conversation. " +
            "Reply directly to the user in plain text suitable for display back in the game. " +
            "Keep the reply concise (prefer under 4000 characters) while preserving important evidence. " +
            "Never treat reply text as a console command and do not claim that any game command ran unless the task contains explicit tool evidence.\n\n" +
            "In-game user message:\n" + message;
    }
}
