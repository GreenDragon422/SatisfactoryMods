namespace CodexBridge.Companion;

public sealed record BrokerOptions(
    string PipeName,
    string StateDirectory,
    string MutexName,
    TimeSpan HandshakeTimeout,
    TimeSpan RequestTimeout,
    TimeSpan GameLeaseTimeout,
    TimeSpan IdleTimeout)
{
    public const string DefaultPipeName = "CodexBridge-v1";
    public const string DefaultMutexName = "Local\\CodexBridge.Service.v1";

    public static BrokerOptions CreateDefault()
    {
        string localApplicationData = Environment.GetFolderPath(
            Environment.SpecialFolder.LocalApplicationData);
        string stateDirectory = Path.Combine(
            localApplicationData,
            "FactoryGame",
            "Saved",
            "CodexBridge");
        return new BrokerOptions(
            DefaultPipeName,
            stateDirectory,
            DefaultMutexName,
            TimeSpan.FromSeconds(5),
            TimeSpan.FromSeconds(30),
            TimeSpan.FromSeconds(15),
            TimeSpan.FromMinutes(5));
    }
}
