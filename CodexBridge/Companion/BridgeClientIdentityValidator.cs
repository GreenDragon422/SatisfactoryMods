using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace CodexBridge.Companion;

public interface IBridgeClientIdentityValidator
{
    bool IsAllowed(string role, int processId, string executablePath);
}

public sealed class BridgeClientIdentityValidator : IBridgeClientIdentityValidator
{
    public bool IsAllowed(string role, int processId, string executablePath)
    {
        if (processId <= 0 || string.IsNullOrWhiteSpace(executablePath))
        {
            return false;
        }

        try
        {
            using Process process = Process.GetProcessById(processId);
            string? actualPath = process.MainModule?.FileName;
            if (actualPath is null || !PathEquals(actualPath, executablePath))
            {
                return false;
            }

            if (role == "mcp")
            {
                string? companionPath = Environment.ProcessPath;
                return companionPath is not null && PathEquals(actualPath, companionPath);
            }

            if (role == "game")
            {
                string fileName = Path.GetFileName(actualPath);
                return fileName.StartsWith("FactoryGame", StringComparison.OrdinalIgnoreCase) &&
                    fileName.EndsWith(".exe", StringComparison.OrdinalIgnoreCase);
            }

            return false;
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

    private static bool PathEquals(string left, string right)
    {
        return string.Equals(
            Path.GetFullPath(left),
            Path.GetFullPath(right),
            StringComparison.OrdinalIgnoreCase);
    }
}

public static class NamedPipeProcessInspector
{
    public static int GetClientProcessId(SafePipeHandle pipeHandle)
    {
        if (!GetNamedPipeClientProcessId(pipeHandle, out uint processId) || processId > int.MaxValue)
        {
            throw new BridgeProtocolException("Unable to verify the named-pipe client process.");
        }

        return (int)processId;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetNamedPipeClientProcessId(
        SafePipeHandle pipeHandle,
        out uint clientProcessId);
}
