using System.ComponentModel;
using ModelContextProtocol;
using ModelContextProtocol.Server;

namespace CodexBridge.Companion;

[McpServerToolType]
public static class CodexBridgeMcpTools
{
    [McpServerTool(
        Name = "game_status",
        Title = "Satisfactory game status",
        ReadOnly = true,
        Destructive = false,
        Idempotent = true,
        OpenWorld = false,
        UseStructuredContent = true)]
    [Description("Report whether the local Satisfactory game bridge is online and identify the live game world, build, mod, and process.")]
    public static Task<BridgeToolResult> GameStatusAsync(
        BridgeToolService bridge,
        CancellationToken cancellationToken)
    {
        return bridge.GameStatusAsync(cancellationToken);
    }

    [McpServerTool(
        Name = "discover_console_commands",
        Title = "Discover Satisfactory console commands",
        ReadOnly = true,
        Destructive = false,
        Idempotent = false,
        OpenWorld = false,
        UseStructuredContent = true)]
    [Description("Search the live game's registered console commands and variables. Use the returned opaque cursor with the same query to read the next page.")]
    public static Task<BridgeToolResult> DiscoverConsoleCommandsAsync(
        BridgeToolService bridge,
        [Description("Case-sensitive substring used by Unreal console discovery. Empty discovers all entries, which may require a narrower query.")]
        string? query = null,
        [Description("Number of entries to return, from 1 through 100.")]
        int limit = 50,
        [Description("Opaque single-use next-page cursor from the preceding discovery response, or null for the first page.")]
        string? cursor = null,
        CancellationToken cancellationToken = default)
    {
        return InvokeValidatedAsync(() => bridge.DiscoverConsoleCommandsAsync(
            query,
            limit,
            cursor,
            cancellationToken));
    }

    [McpServerTool(
        Name = "execute_console_command",
        Title = "Execute a Satisfactory console command",
        ReadOnly = false,
        Destructive = true,
        Idempotent = false,
        OpenWorld = false,
        UseStructuredContent = true)]
    [Description("Execute one built-in or mod-provided command through Unreal's generic console dispatcher and capture its synchronous output. Discover the command first when its exact name or arguments are unknown.")]
    public static Task<BridgeToolResult> ExecuteConsoleCommandAsync(
        BridgeToolService bridge,
        [Description("The complete console command line, including arguments.")]
        string command,
        [Description("Optional stable retry identifier. Reuse it only when retrying the same uncertain command; a delivery_unknown result means the command may already have run.")]
        string? requestId = null,
        CancellationToken cancellationToken = default)
    {
        return InvokeValidatedAsync(() => bridge.ExecuteConsoleCommandAsync(
            command,
            requestId,
            cancellationToken));
    }

    [McpServerTool(
        Name = "send_game_message",
        Title = "Send a message to Satisfactory",
        ReadOnly = false,
        Destructive = false,
        Idempotent = false,
        OpenWorld = false,
        UseStructuredContent = true)]
    [Description("Display an explicitly attributed 'Codex:' message in the running game and record it in the game log. This never executes reply text as a console command.")]
    public static Task<BridgeToolResult> SendGameMessageAsync(
        BridgeToolService bridge,
        [Description("Message text to display after the Codex attribution.")]
        string message,
        CancellationToken cancellationToken = default)
    {
        return InvokeValidatedAsync(() => bridge.SendGameMessageAsync(message, cancellationToken));
    }

    [McpServerTool(
        Name = "recent_game_log",
        Title = "Read recent Satisfactory log lines",
        ReadOnly = true,
        Destructive = false,
        Idempotent = true,
        OpenWorld = false,
        UseStructuredContent = true)]
    [Description("Read a bounded tail of the live Satisfactory FactoryGame log to explain what is actually happening in the installed game.")]
    public static Task<BridgeToolResult> ReadRecentGameLogAsync(
        BridgeToolService bridge,
        [Description("Maximum number of recent lines to return, from 1 through 500.")]
        int maxLines = 200,
        CancellationToken cancellationToken = default)
    {
        return InvokeValidatedAsync(() => bridge.ReadRecentGameLogAsync(
            maxLines,
            cancellationToken));
    }

    [McpServerTool(
        Name = "read_result_page",
        Title = "Read another command-output page",
        ReadOnly = true,
        Destructive = false,
        Idempotent = false,
        OpenWorld = false,
        UseStructuredContent = true)]
    [Description("Read the next bounded page of a large console-command result. Result cursors are opaque, single-use, and expire.")]
    public static Task<BridgeToolResult> ReadResultPageAsync(
        BridgeToolService bridge,
        [Description("Opaque result identifier returned by execute_console_command or the preceding page.")]
        string resultId,
        [Description("Opaque single-use page cursor returned with the result identifier.")]
        string cursor,
        CancellationToken cancellationToken = default)
    {
        return InvokeValidatedAsync(() => bridge.ReadResultPageAsync(
            resultId,
            cursor,
            cancellationToken));
    }

    [McpServerTool(
        Name = "bridge_conversation",
        Title = "Inspect or reset the in-game Codex conversation",
        ReadOnly = false,
        Destructive = false,
        Idempotent = true,
        OpenWorld = false,
        UseStructuredContent = true)]
    [Description("Report the dedicated Codex task used for game-originated messages, its workspace, and pending count. Set reset=true to forget only that bridge task identity so the next game message creates a new task.")]
    public static Task<BridgeToolResult> BridgeConversationAsync(
        BridgeToolService bridge,
        [Description("False reads the current bridge conversation; true forgets its task identity without deleting the existing task history.")]
        bool reset = false,
        CancellationToken cancellationToken = default)
    {
        return bridge.BridgeConversationAsync(reset, cancellationToken);
    }

    private static async Task<BridgeToolResult> InvokeValidatedAsync(
        Func<Task<BridgeToolResult>> operation)
    {
        try
        {
            return await operation().ConfigureAwait(false);
        }
        catch (ArgumentException exception)
        {
            throw new McpException(exception.Message);
        }
    }
}
