using System.Diagnostics;

namespace CodexBridge.Companion;

public interface IBridgeServiceProcessLauncher
{
    void Launch();
}

public sealed class BridgeServiceProcessLauncher : IBridgeServiceProcessLauncher
{
    public void Launch()
    {
        string executablePath = Environment.ProcessPath ??
            throw new InvalidOperationException("The companion executable path is unavailable.");
        ProcessStartInfo startInfo = new()
        {
            FileName = executablePath,
            Arguments = "--service",
            CreateNoWindow = true,
            UseShellExecute = false,
            WorkingDirectory = Path.GetDirectoryName(executablePath) ?? Environment.CurrentDirectory
        };
        using Process process = Process.Start(startInfo) ??
            throw new InvalidOperationException("The CodexBridge broker process did not start.");
    }
}

public static class BrokerServiceLauncher
{
    public static async Task<BrokerClient> StartOrConnectAsync(
        BrokerOptions options,
        string role,
        string clientInstanceId,
        long generation,
        IBridgeServiceProcessLauncher processLauncher,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(processLauncher);
        BrokerClient? existingClient = await TryConnectAsync(
            options,
            role,
            clientInstanceId,
            generation,
            cancellationToken).ConfigureAwait(false);
        if (existingClient is not null)
        {
            return existingClient;
        }

        processLauncher.Launch();
        TimeSpan delay = TimeSpan.FromMilliseconds(50);
        DateTimeOffset deadline = DateTimeOffset.UtcNow.AddSeconds(8);
        while (DateTimeOffset.UtcNow < deadline)
        {
            cancellationToken.ThrowIfCancellationRequested();
            await Task.Delay(delay, cancellationToken).ConfigureAwait(false);
            BrokerClient? launchedClient = await TryConnectAsync(
                options,
                role,
                clientInstanceId,
                generation,
                cancellationToken).ConfigureAwait(false);
            if (launchedClient is not null)
            {
                return launchedClient;
            }

            delay = TimeSpan.FromMilliseconds(Math.Min(delay.TotalMilliseconds * 2, 500));
        }

        throw new TimeoutException("CodexBridge broker did not become ready within 8 seconds.");
    }

    private static async Task<BrokerClient?> TryConnectAsync(
        BrokerOptions options,
        string role,
        string clientInstanceId,
        long generation,
        CancellationToken cancellationToken)
    {
        string token;
        try
        {
            token = BridgeTokenStore.Read(options.StateDirectory);
        }
        catch (FileNotFoundException)
        {
            return null;
        }
        catch (DirectoryNotFoundException)
        {
            return null;
        }

        try
        {
            return await BrokerClient.ConnectAsync(
                options,
                role,
                clientInstanceId,
                generation,
                token,
                cancellationToken).ConfigureAwait(false);
        }
        catch (IOException)
        {
            return null;
        }
        catch (TimeoutException)
        {
            return null;
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            return null;
        }
    }
}
