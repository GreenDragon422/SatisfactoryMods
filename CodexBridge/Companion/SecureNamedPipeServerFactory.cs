using System.ComponentModel;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Security.AccessControl;
using System.Security.Principal;
using Microsoft.Win32.SafeHandles;

namespace CodexBridge.Companion;

public static class SecureNamedPipeServerFactory
{
    private const uint PipeAccessDuplex = 0x00000003;
    private const uint FileFlagOverlapped = 0x40000000;
    private const uint PipeRejectRemoteClients = 0x00000008;
    private const uint MaximumInstances = 255;

    [SupportedOSPlatform("windows")]
    public static NamedPipeServerStream Create(string pipeName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(pipeName);
        using WindowsIdentity identity = WindowsIdentity.GetCurrent();
        SecurityIdentifier user = identity.User ??
            throw new InvalidOperationException("The current Windows user SID is unavailable.");
        PipeSecurity security = new();
        security.SetOwner(user);
        security.SetAccessRuleProtection(isProtected: true, preserveInheritance: false);
        security.AddAccessRule(new PipeAccessRule(
            user,
            PipeAccessRights.FullControl,
            AccessControlType.Allow));
        byte[] descriptor = security.GetSecurityDescriptorBinaryForm();
        GCHandle descriptorHandle = GCHandle.Alloc(descriptor, GCHandleType.Pinned);
        try
        {
            SecurityAttributes attributes = new()
            {
                Length = Marshal.SizeOf<SecurityAttributes>(),
                SecurityDescriptor = descriptorHandle.AddrOfPinnedObject(),
                InheritHandle = false
            };
            SafePipeHandle handle = CreateNamedPipe(
                $"\\\\.\\pipe\\{pipeName}",
                PipeAccessDuplex | FileFlagOverlapped,
                PipeRejectRemoteClients,
                MaximumInstances,
                0,
                0,
                0,
                ref attributes);
            if (handle.IsInvalid)
            {
                int error = Marshal.GetLastPInvokeError();
                handle.Dispose();
                throw new Win32Exception(error, "Unable to create the secure CodexBridge pipe.");
            }

            try
            {
                return new NamedPipeServerStream(
                    PipeDirection.InOut,
                    isAsync: true,
                    isConnected: false,
                    handle);
            }
            catch
            {
                handle.Dispose();
                throw;
            }
        }
        finally
        {
            descriptorHandle.Free();
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct SecurityAttributes
    {
        public int Length;
        public IntPtr SecurityDescriptor;

        [MarshalAs(UnmanagedType.Bool)]
        public bool InheritHandle;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafePipeHandle CreateNamedPipe(
        string pipeName,
        uint openMode,
        uint pipeMode,
        uint maximumInstances,
        uint outputBufferSize,
        uint inputBufferSize,
        uint defaultTimeout,
        ref SecurityAttributes securityAttributes);
}
