using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace EightAxis.WinRemote.Config;

public sealed record AppSettings(
    RemoteSourceMode SourceMode,
    RelaySecurityProfile RelaySecurity,
    string EvidenceDirectory,
    bool ViewOnly,
    bool EnablePointer,
    bool EnableRemoteInput)
{
    public const string DefaultRelayUrl = "https://192.168.1.221:18080/";

    public Uri RelayBaseUri => RelaySecurity.RelayBaseUri;

    public static AppSettings LoadFromEnvironment()
    {
        string[] args = Environment.GetCommandLineArgs();
        return Load(args, Environment.GetEnvironmentVariables(), AppContext.BaseDirectory);
    }

    public static AppSettings Load(
        IReadOnlyList<string> args,
        System.Collections.IDictionary environment,
        string baseDirectory,
        string? authorizedSecurityRoot = null)
    {
        ConfigFileSettings config = LoadConfigFile(ReadOption(args, "--config"), baseDirectory);
        string evidenceDirectory = ReadOption(args, "--evidence-dir")
            ?? ReadEnvironment(environment, "8AX_REMOTE_EVIDENCE_DIR")
            ?? config.EvidenceDirectory
            ?? Path.Combine(Path.GetTempPath(), "8ax-win", "evidence");

        string? securityProfilePath = ReadOption(args, "--relay-security-profile")
            ?? ReadEnvironment(environment, "8AX_REMOTE_SECURITY_PROFILE")
            ?? config.RelaySecurityProfile;
        if (String.IsNullOrWhiteSpace(securityProfilePath))
        {
            throw new InvalidOperationException(
                "WinRemote requires an explicit --relay-security-profile under D:\\授权私钥; anonymous relay fallback is disabled.");
        }
        RelaySecurityProfile relaySecurity = RelaySecurityProfile.Load(
            securityProfilePath,
            authorizedSecurityRoot);

        string relayUrl = ReadOption(args, "--relay")
            ?? ReadEnvironment(environment, "8AX_REMOTE_RELAY_URL")
            ?? config.RelayUrl
            ?? relaySecurity.RelayBaseUri.ToString();

        if (!Uri.TryCreate(relayUrl, UriKind.Absolute, out Uri? relayUri)
            || relayUri.Scheme != Uri.UriSchemeHttps)
        {
            throw new InvalidOperationException("WinRemote relay URL must use HTTPS.");
        }
        relayUri = NormalizeRelayBaseUri(relayUri);
        if (relayUri != relaySecurity.RelayBaseUri)
        {
            throw new InvalidOperationException(
                "WinRemote relay URL does not match the selected pinned security profile.");
        }
        bool viewOnly = ReadBoolOption(args, "--view-only")
            ?? ReadBoolEnvironment(environment, "8AX_REMOTE_VIEW_ONLY")
            ?? config.ViewOnly
            ?? false;
        bool enablePointer = ReadBoolOption(args, "--enable-pointer")
            ?? ReadBoolEnvironment(environment, "8AX_REMOTE_ENABLE_POINTER")
            ?? config.EnablePointer
            ?? false;
        bool enableRemoteInput = ReadBoolOption(args, "--enable-remote-input")
            ?? ReadBoolEnvironment(environment, "8AX_REMOTE_ENABLE_REMOTE_INPUT")
            ?? config.EnableRemoteInput
            ?? !viewOnly;

        return new AppSettings(
            RemoteSourceMode.Relay,
            relaySecurity,
            evidenceDirectory,
            viewOnly,
            enablePointer,
            enableRemoteInput);
    }

    private static string? ReadOption(IReadOnlyList<string> args, string optionName)
    {
        for (int i = 0; i < args.Count; i++)
        {
            string arg = args[i];
            if (String.Equals(arg, optionName, StringComparison.OrdinalIgnoreCase) && i + 1 < args.Count)
            {
                return args[i + 1];
            }

            string prefix = optionName + "=";
            if (arg.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            {
                return arg[prefix.Length..];
            }
        }

        return null;
    }

    private static bool? ReadBoolOption(IReadOnlyList<string> args, string optionName)
    {
        string? value = ReadOption(args, optionName);
        if (String.IsNullOrWhiteSpace(value))
        {
            return null;
        }

        return Boolean.TryParse(value, out bool parsed) ? parsed : null;
    }

    private static string? ReadEnvironment(System.Collections.IDictionary environment, string key) =>
        environment.Contains(key) ? environment[key]?.ToString() : null;

    private static bool? ReadBoolEnvironment(System.Collections.IDictionary environment, string key)
    {
        string? value = ReadEnvironment(environment, key);
        if (String.IsNullOrWhiteSpace(value))
        {
            return null;
        }

        return Boolean.TryParse(value, out bool parsed) ? parsed : null;
    }

    private static ConfigFileSettings LoadConfigFile(string? explicitPath, string baseDirectory)
    {
        string? path = explicitPath;
        if (String.IsNullOrWhiteSpace(path))
        {
            string defaultPath = Path.Combine(baseDirectory, "8ax.WinRemote.json");
            if (File.Exists(defaultPath))
            {
                path = defaultPath;
            }
        }

        if (String.IsNullOrWhiteSpace(path) || !File.Exists(path))
        {
            return new ConfigFileSettings();
        }

        string json = File.ReadAllText(path);
        ConfigFileSettings? settings = JsonSerializer.Deserialize<ConfigFileSettings>(
            json,
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
        return settings ?? new ConfigFileSettings();
    }

    private static Uri NormalizeRelayBaseUri(Uri relayUri)
    {
        string uri = relayUri.ToString();
        if (!uri.EndsWith("/", StringComparison.Ordinal))
        {
            uri += "/";
        }

        return new Uri(uri);
    }
}

public sealed record ConfigFileSettings
{
    [JsonPropertyName("relay_url")]
    public string? RelayUrl { get; init; }

    [JsonPropertyName("relay_security_profile")]
    public string? RelaySecurityProfile { get; init; }

    [JsonPropertyName("evidence_dir")]
    public string? EvidenceDirectory { get; init; }

    [JsonPropertyName("view_only")]
    public bool? ViewOnly { get; init; }

    [JsonPropertyName("enable_pointer")]
    public bool? EnablePointer { get; init; }

    [JsonPropertyName("enable_remote_input")]
    public bool? EnableRemoteInput { get; init; }
}

public enum RemoteSourceMode
{
    Relay,
}
