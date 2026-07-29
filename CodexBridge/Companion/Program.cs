using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using ModelContextProtocol.Server;

namespace CodexBridge.Companion;

public static class Program
{
    public static async Task<int> Main(string[] arguments)
    {
        if (arguments.Length != 1)
        {
            WriteUsage();
            return 2;
        }

        BrokerOptions options = BrokerOptions.CreateDefault();
        if (string.Equals(arguments[0], "--service", StringComparison.Ordinal))
        {
            return await RunServiceAsync(options).ConfigureAwait(false);
        }

        if (string.Equals(arguments[0], "--probe", StringComparison.Ordinal))
        {
            return await RunProbeAsync(options).ConfigureAwait(false);
        }

        if (string.Equals(arguments[0], "--smoke-game", StringComparison.Ordinal))
        {
            return await RunGameSmokeAsync(options).ConfigureAwait(false);
        }

        if (string.Equals(arguments[0], "--mcp", StringComparison.Ordinal))
        {
            return await RunMcpAsync(options).ConfigureAwait(false);
        }

        WriteUsage();
        return 2;
    }

    private static async Task<int> RunServiceAsync(BrokerOptions options)
    {
        using CancellationTokenSource shutdown = new();
        Console.CancelKeyPress += HandleCancel;
        void HandleCancel(object? sender, ConsoleCancelEventArgs eventArguments)
        {
            eventArguments.Cancel = true;
            shutdown.Cancel();
        }

        try
        {
            await using BrokerService service = new(
                options,
                new BridgeClientIdentityValidator());
            await service.RunAsync(shutdown.Token).ConfigureAwait(false);
            return 0;
        }
        catch (InvalidOperationException exception)
        {
            Console.Error.WriteLine(exception.Message);
            return 3;
        }
        finally
        {
            Console.CancelKeyPress -= HandleCancel;
        }
    }

    private static async Task<int> RunProbeAsync(BrokerOptions options)
    {
        try
        {
            await using BrokerClient client = await BrokerServiceLauncher.StartOrConnectAsync(
                options,
                "mcp",
                Guid.NewGuid().ToString("N"),
                0,
                new BridgeServiceProcessLauncher(),
                CancellationToken.None).ConfigureAwait(false);
            BridgeResponse response = await client.RequestAsync(
                "service.status",
                BridgeJson.EmptyObject,
                CancellationToken.None).ConfigureAwait(false);
            Console.Out.WriteLine(response.Payload.GetRawText());
            return response.Ok ? 0 : 4;
        }
        catch (Exception exception) when (
            exception is IOException or TimeoutException or BridgeProtocolException)
        {
            Console.Error.WriteLine(exception.Message);
            return 4;
        }
    }

    private static async Task<int> RunGameSmokeAsync(BrokerOptions options)
    {
        try
        {
            await using BrokerClient client = await BrokerServiceLauncher.StartOrConnectAsync(
                options,
                "mcp",
                Guid.NewGuid().ToString("N"),
                0,
                new BridgeServiceProcessLauncher(),
                CancellationToken.None).ConfigureAwait(false);
            BridgeResponse status = await client.RequestAsync(
                "game.status",
                BridgeJson.EmptyObject,
                CancellationToken.None).ConfigureAwait(false);
            BridgeResponse discovery = await client.RequestAsync(
                "console.discover",
                BridgeJson.ToElement(new { query = "CodexBridge.", limit = 20 }),
                CancellationToken.None).ConfigureAwait(false);
            BridgeResponse command = await client.RequestAsync(
                "console.execute",
                BridgeJson.ToElement(new { command = "CodexBridge.Status" }),
                CancellationToken.None).ConfigureAwait(false);
            bool gameReady = status.Ok &&
                status.Payload.TryGetProperty("gameReady", out System.Text.Json.JsonElement readyElement) &&
                readyElement.ValueKind == System.Text.Json.JsonValueKind.True;
            bool discoveryVerified = discovery.Ok &&
                ContainsDiscoveredCommand(discovery.Payload, "CodexBridge.Status");
            bool commandVerified = command.Ok &&
                command.Payload.TryGetProperty("handled", out System.Text.Json.JsonElement handledElement) &&
                handledElement.ValueKind == System.Text.Json.JsonValueKind.True &&
                command.Payload.TryGetProperty("output", out System.Text.Json.JsonElement outputElement) &&
                outputElement.GetString()?.Contains("CodexBridge:", StringComparison.Ordinal) == true;
            bool ok = gameReady && discoveryVerified && commandVerified;
            Console.Out.WriteLine(BridgeJson.ToElement(new
            {
                ok,
                gameReady,
                discoveryVerified,
                commandVerified,
                status = ToOutput(status),
                discovery = ToOutput(discovery),
                command = ToOutput(command)
            }).GetRawText());
            return ok ? 0 : 5;
        }
        catch (Exception exception) when (
            exception is IOException or TimeoutException or BridgeProtocolException)
        {
            Console.Error.WriteLine(exception.Message);
            return 5;
        }
    }

    private static async Task<int> RunMcpAsync(BrokerOptions options)
    {
        Microsoft.Extensions.Hosting.HostApplicationBuilder builder =
            Microsoft.Extensions.Hosting.Host.CreateApplicationBuilder();
        builder.Logging.ClearProviders();
        builder.Logging.AddConsole(consoleOptions =>
        {
            consoleOptions.LogToStandardErrorThreshold = Microsoft.Extensions.Logging.LogLevel.Trace;
        });
        builder.Logging.SetMinimumLevel(Microsoft.Extensions.Logging.LogLevel.Warning);
        builder.Services.AddSingleton(options);
        builder.Services.AddSingleton<IBridgeServiceProcessLauncher, BridgeServiceProcessLauncher>();
        builder.Services.AddSingleton<IBridgeBrokerConnector, BridgeBrokerConnector>();
        builder.Services.AddSingleton<IBridgeToolTransport, BrokerBridgeToolTransport>();
        builder.Services.AddSingleton<BridgeToolService>();
        builder.Services
            .AddMcpServer(serverOptions =>
            {
                serverOptions.ServerInstructions =
                    "This server reflects the live local Satisfactory process, not source-tree assumptions. " +
                    "Call discover_console_commands before execute_console_command when the exact command or arguments are unknown. " +
                    "Console execution can change or destroy game state, is never retried automatically, and delivery_unknown means it may already have run. " +
                    "Opaque cursors are single-use and expire; use read_result_page for additional command output.";
            })
            .WithStdioServerTransport()
            .WithToolsFromAssembly();
        using Microsoft.Extensions.Hosting.IHost host = builder.Build();
        await host.RunAsync().ConfigureAwait(false);
        return 0;
    }

    private static bool ContainsDiscoveredCommand(
        System.Text.Json.JsonElement payload,
        string commandName)
    {
        if (!payload.TryGetProperty("items", out System.Text.Json.JsonElement items) ||
            items.ValueKind != System.Text.Json.JsonValueKind.Array)
        {
            return false;
        }

        foreach (System.Text.Json.JsonElement item in items.EnumerateArray())
        {
            if (item.TryGetProperty("name", out System.Text.Json.JsonElement name) &&
                string.Equals(name.GetString(), commandName, StringComparison.Ordinal))
            {
                return true;
            }
        }

        return false;
    }

    private static object ToOutput(BridgeResponse response)
    {
        return new
        {
            response.Ok,
            response.Error,
            payload = response.Payload
        };
    }

    private static void WriteUsage()
    {
        Console.Error.WriteLine(
            "Usage: CodexBridge.Companion --service|--probe|--smoke-game|--mcp");
    }
}
