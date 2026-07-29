using System.Security.AccessControl;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Runtime.Versioning;

namespace CodexBridge.Companion;

public static class BridgeTokenStore
{
    private const string TokenFileName = "broker.token";

    public static string GetOrCreate(string stateDirectory)
    {
        EnsureStateDirectory(stateDirectory);
        string tokenPath = GetTokenPath(stateDirectory);
        if (File.Exists(tokenPath))
        {
            try
            {
                ApplyCurrentUserOnlyAcl(tokenPath);
                return Read(stateDirectory);
            }
            catch (BridgeProtocolException)
            {
                File.Delete(tokenPath);
            }
        }

        string token = Convert.ToHexString(RandomNumberGenerator.GetBytes(32));
        string temporaryPath = Path.Combine(
            stateDirectory,
            $".{TokenFileName}.{Guid.NewGuid():N}.tmp");
        try
        {
            using (FileStream stream = new(
                temporaryPath,
                FileMode.CreateNew,
                FileAccess.Write,
                FileShare.None,
                bufferSize: 4096,
                FileOptions.WriteThrough))
            {
                using StreamWriter writer = new(stream, leaveOpen: true);
                writer.Write(token);
                writer.Flush();
                stream.Flush(flushToDisk: true);
            }

            ApplyCurrentUserOnlyAcl(temporaryPath);
            try
            {
                File.Move(temporaryPath, tokenPath);
                return Read(stateDirectory);
            }
            catch (IOException) when (File.Exists(tokenPath))
            {
                return Read(stateDirectory);
            }
        }
        finally
        {
            File.Delete(temporaryPath);
        }
    }

    public static string Read(string stateDirectory)
    {
        string tokenPath = GetTokenPath(stateDirectory);
        VerifyCurrentUserOnlyAcl(tokenPath);
        string token = File.ReadAllText(tokenPath).Trim();
        if (token.Length != 64 || !token.All(Uri.IsHexDigit))
        {
            throw new BridgeProtocolException("The bridge handshake token file is invalid.");
        }

        return token;
    }

    private static string GetTokenPath(string stateDirectory)
    {
        return Path.Combine(stateDirectory, TokenFileName);
    }

    private static void EnsureStateDirectory(string stateDirectory)
    {
        Directory.CreateDirectory(stateDirectory);
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        SecurityIdentifier user = GetCurrentUserSid();
        DirectorySecurity security = new();
        security.SetOwner(user);
        security.SetAccessRuleProtection(isProtected: true, preserveInheritance: false);
        security.AddAccessRule(new FileSystemAccessRule(
            user,
            FileSystemRights.FullControl,
            InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit,
            PropagationFlags.None,
            AccessControlType.Allow));
        DirectoryInfo directory = new(stateDirectory);
        directory.SetAccessControl(security);
    }

    private static void ApplyCurrentUserOnlyAcl(string tokenPath)
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        SecurityIdentifier user = GetCurrentUserSid();
        FileSecurity security = new();
        security.SetOwner(user);
        security.SetAccessRuleProtection(isProtected: true, preserveInheritance: false);
        security.AddAccessRule(new FileSystemAccessRule(
            user,
            FileSystemRights.FullControl,
            AccessControlType.Allow));
        FileInfo tokenFile = new(tokenPath);
        tokenFile.SetAccessControl(security);
    }

    private static void VerifyCurrentUserOnlyAcl(string tokenPath)
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        SecurityIdentifier user = GetCurrentUserSid();
        FileSecurity security = new FileInfo(tokenPath).GetAccessControl(
            AccessControlSections.Access | AccessControlSections.Owner);
        IdentityReference? owner = security.GetOwner(typeof(SecurityIdentifier));
        AuthorizationRuleCollection rules = security.GetAccessRules(
            includeExplicit: true,
            includeInherited: true,
            typeof(SecurityIdentifier));
        bool hasUserAllow = false;
        foreach (FileSystemAccessRule rule in rules)
        {
            if (rule.IsInherited ||
                rule.AccessControlType != AccessControlType.Allow ||
                rule.IdentityReference is not SecurityIdentifier identity)
            {
                throw new BridgeProtocolException("The bridge token ACL is not current-user-only.");
            }

            if (!identity.Equals(user))
            {
                throw new BridgeProtocolException("The bridge token ACL permits another identity.");
            }

            hasUserAllow = true;
        }

        if (owner is null || !owner.Equals(user) || !security.AreAccessRulesProtected || !hasUserAllow)
        {
            throw new BridgeProtocolException("The bridge token ACL is not current-user-only.");
        }
    }

    [SupportedOSPlatform("windows")]
    private static SecurityIdentifier GetCurrentUserSid()
    {
        using WindowsIdentity identity = WindowsIdentity.GetCurrent();
        return identity.User ??
            throw new InvalidOperationException("The current Windows user SID is unavailable.");
    }
}
