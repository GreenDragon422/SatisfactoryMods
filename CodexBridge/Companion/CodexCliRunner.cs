using System.ComponentModel;
using System.Diagnostics;
using System.Text;

namespace CodexBridge.Companion;

public sealed class CodexCliRunner : ICodexCliRunner
{
    private const int MaximumStandardErrorLength = 65536;

    private readonly string? codexExecutable;
    private readonly TimeSpan turnTimeout;

    public CodexCliRunner(string? codexExecutable = null, TimeSpan? turnTimeout = null)
    {
        this.codexExecutable = codexExecutable ?? CodexCliLocator.Find();
        this.turnTimeout = turnTimeout ?? TimeSpan.FromMinutes(10);
    }

    public async Task<CodexCliTurnResult> RunAsync(
        string? threadId,
        string prompt,
        string workspaceDirectory,
        CancellationToken cancellationToken)
    {
        if (codexExecutable is null || !File.Exists(codexExecutable))
        {
            return new CodexCliTurnResult(false, threadId, null, "codex_cli_not_found", false);
        }

        if (!Directory.Exists(workspaceDirectory))
        {
            return new CodexCliTurnResult(false, threadId, null, "workspace_not_found", false);
        }

        ProcessStartInfo startInfo = CreateStartInfo(
            codexExecutable,
            threadId,
            workspaceDirectory);
        Process? startedProcess;
        try
        {
            startedProcess = Process.Start(startInfo);
        }
        catch (Exception exception) when (
            exception is Win32Exception or InvalidOperationException)
        {
            return new CodexCliTurnResult(
                false,
                threadId,
                null,
                "codex_cli_start_failed",
                false);
        }

        if (startedProcess is null)
        {
            return new CodexCliTurnResult(
                false,
                threadId,
                null,
                "codex_cli_start_failed",
                false);
        }

        using Process process = startedProcess;
        CodexCliEventAccumulator accumulator = new();
        StringBuilder standardError = new();
        Task outputTask = ReadOutputAsync(process, accumulator);
        Task errorTask = ReadErrorAsync(process, standardError);
        await process.StandardInput.WriteAsync(prompt.AsMemory(), cancellationToken).ConfigureAwait(false);
        process.StandardInput.Close();

        using CancellationTokenSource timeout = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken);
        timeout.CancelAfter(turnTimeout);
        try
        {
            await process.WaitForExitAsync(timeout.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            if (!process.HasExited)
            {
                process.Kill(true);
            }

            await process.WaitForExitAsync(CancellationToken.None).ConfigureAwait(false);
            await Task.WhenAll(outputTask, errorTask).ConfigureAwait(false);
            if (cancellationToken.IsCancellationRequested)
            {
                throw;
            }

            return new CodexCliTurnResult(false, threadId, null, "codex_turn_timeout", false);
        }

        await Task.WhenAll(outputTask, errorTask).ConfigureAwait(false);
        return accumulator.Complete(process.ExitCode, standardError.ToString());
    }

    private static ProcessStartInfo CreateStartInfo(
        string executable,
        string? threadId,
        string workspaceDirectory)
    {
        ProcessStartInfo startInfo = new(executable)
        {
            WorkingDirectory = workspaceDirectory,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true
        };
        startInfo.ArgumentList.Add("exec");
        if (threadId is not null)
        {
            startInfo.ArgumentList.Add("resume");
        }

        startInfo.ArgumentList.Add("--json");
        startInfo.ArgumentList.Add("--ignore-user-config");
        startInfo.ArgumentList.Add("--skip-git-repo-check");
        if (threadId is null)
        {
            startInfo.ArgumentList.Add("--sandbox");
            startInfo.ArgumentList.Add("workspace-write");
            startInfo.ArgumentList.Add("-C");
            startInfo.ArgumentList.Add(workspaceDirectory);
        }
        else
        {
            startInfo.ArgumentList.Add("--all");
            startInfo.ArgumentList.Add(threadId);
        }

        startInfo.ArgumentList.Add("-");
        return startInfo;
    }

    private static async Task ReadOutputAsync(
        Process process,
        CodexCliEventAccumulator accumulator)
    {
        while (true)
        {
            string? line = await process.StandardOutput.ReadLineAsync().ConfigureAwait(false);
            if (line is null)
            {
                return;
            }

            accumulator.AddLine(line);
        }
    }

    private static async Task ReadErrorAsync(Process process, StringBuilder standardError)
    {
        while (true)
        {
            string? line = await process.StandardError.ReadLineAsync().ConfigureAwait(false);
            if (line is null)
            {
                return;
            }

            if (standardError.Length > 0)
            {
                standardError.AppendLine();
            }

            standardError.Append(line);
            if (standardError.Length > MaximumStandardErrorLength)
            {
                standardError.Remove(0, standardError.Length - MaximumStandardErrorLength);
            }
        }
    }
}
