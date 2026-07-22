using System.IO;
using System.Net.Security;
using System.Net.Http;
using System.Net.WebSockets;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace EightAxis.WinRemote.Config;

public sealed class RelaySecurityProfile
{
    public const string Schema = "v5.winremote_relay_security.v1";
    public const string AuthenticationProtocol = "v5.remote.auth.v1";
    public const string SessionScheme = "V5Session";
    public const string DefaultAuthorizedRoot = @"D:\授权私钥";

    private static readonly Regex DeviceIdPattern = new("^[0-9]{6}$", RegexOptions.CultureInvariant);
    private static readonly Regex ClientIdPattern = new("^[A-Za-z0-9._-]{1,64}$", RegexOptions.CultureInvariant);
    private static readonly HashSet<string> AllowedScopes = new(StringComparer.Ordinal)
    {
        "viewer",
        "diagnostics",
        "program_manager",
        "operator",
        "ota_admin",
    };

    private readonly byte[] _certificateSha256;
    private readonly byte[] _clientSecret;
    private readonly string[] _scopes;

    private RelaySecurityProfile(
        Uri relayBaseUri,
        string deviceId,
        byte[] certificateSha256,
        string clientId,
        byte[] clientSecret,
        string[] scopes,
        string sourcePath)
    {
        RelayBaseUri = relayBaseUri;
        DeviceId = deviceId;
        _certificateSha256 = certificateSha256;
        CertificateSha256 = Convert.ToHexString(certificateSha256).ToLowerInvariant();
        ClientId = clientId;
        _clientSecret = clientSecret;
        _scopes = scopes;
        Scopes = Array.AsReadOnly(_scopes);
        SourcePath = sourcePath;
    }

    public Uri RelayBaseUri { get; }
    public string DeviceId { get; }
    public string CertificateSha256 { get; }
    public string ClientId { get; }
    public IReadOnlyList<string> Scopes { get; }
    public string SourcePath { get; }

    public static RelaySecurityProfile Load(string path, string? authorizedRoot = null)
    {
        if (String.IsNullOrWhiteSpace(path) || !Path.IsPathFullyQualified(path))
        {
            throw new InvalidOperationException("WinRemote relay security profile must be an explicit absolute path.");
        }

        string root = Path.GetFullPath(authorizedRoot ?? DefaultAuthorizedRoot)
            .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        string fullPath = Path.GetFullPath(path);
        string relative = Path.GetRelativePath(root, fullPath);
        if (relative == ".." || relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal)
            || Path.IsPathFullyQualified(relative))
        {
            throw new InvalidOperationException($"WinRemote relay security profile must stay under {root}.");
        }
        if (!File.Exists(fullPath))
        {
            throw new FileNotFoundException("WinRemote relay security profile is missing.", fullPath);
        }
        if ((File.GetAttributes(fullPath) & FileAttributes.ReparsePoint) != 0)
        {
            throw new InvalidOperationException("WinRemote relay security profile must not be a link or reparse point.");
        }

        FileInfo info = new(fullPath);
        if (info.Length <= 0 || info.Length > 64 * 1024)
        {
            throw new InvalidOperationException("WinRemote relay security profile size is invalid.");
        }
        byte[] bytes = File.ReadAllBytes(fullPath);
        using JsonDocument document = JsonDocument.Parse(bytes, new JsonDocumentOptions
        {
            AllowTrailingCommas = false,
            CommentHandling = JsonCommentHandling.Disallow,
            MaxDepth = 8,
        });
        JsonElement rootElement = document.RootElement;
        if (rootElement.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidOperationException("WinRemote relay security profile root must be an object.");
        }
        Dictionary<string, JsonElement> fields = new(StringComparer.Ordinal);
        foreach (JsonProperty property in rootElement.EnumerateObject())
        {
            if (!fields.TryAdd(property.Name, property.Value))
            {
                throw new InvalidOperationException($"WinRemote relay security profile duplicates field {property.Name}.");
            }
        }
        string[] expectedFields =
        [
            "schema",
            "device_id",
            "relay_base_uri",
            "certificate_sha256",
            "client_id",
            "client_secret_base64",
            "scopes",
        ];
        if (fields.Count != expectedFields.Length || expectedFields.Any(name => !fields.ContainsKey(name)))
        {
            throw new InvalidOperationException("WinRemote relay security profile fields are invalid.");
        }
        string schema = RequiredString(fields, "schema");
        string deviceId = RequiredString(fields, "device_id");
        string relayBaseUriText = RequiredString(fields, "relay_base_uri");
        string fingerprintText = RequiredString(fields, "certificate_sha256").ToLowerInvariant();
        string clientId = RequiredString(fields, "client_id");
        string secretText = RequiredString(fields, "client_secret_base64");
        if (schema != Schema)
        {
            throw new InvalidOperationException("WinRemote relay security profile schema is invalid.");
        }
        if (!DeviceIdPattern.IsMatch(deviceId))
        {
            throw new InvalidOperationException("WinRemote relay device_id must contain six decimal digits.");
        }
        if (!ClientIdPattern.IsMatch(clientId))
        {
            throw new InvalidOperationException("WinRemote relay client_id is invalid.");
        }
        if (!Uri.TryCreate(relayBaseUriText, UriKind.Absolute, out Uri? relayBaseUri)
            || relayBaseUri.Scheme != Uri.UriSchemeHttps
            || !String.IsNullOrEmpty(relayBaseUri.UserInfo)
            || !String.IsNullOrEmpty(relayBaseUri.Query)
            || !String.IsNullOrEmpty(relayBaseUri.Fragment)
            || relayBaseUri.AbsolutePath != "/")
        {
            throw new InvalidOperationException("WinRemote relay base URI must be an HTTPS origin ending in '/'.");
        }
        byte[] fingerprint = DecodeHex(fingerprintText, 32, "certificate_sha256");
        byte[] secret;
        try
        {
            secret = Convert.FromBase64String(secretText);
        }
        catch (FormatException ex)
        {
            throw new InvalidOperationException("WinRemote relay client secret is not valid base64.", ex);
        }
        if (secret.Length != 32)
        {
            throw new InvalidOperationException("WinRemote relay client secret must contain 256 bits.");
        }
        JsonElement scopesElement = fields["scopes"];
        if (scopesElement.ValueKind != JsonValueKind.Array)
        {
            throw new InvalidOperationException("WinRemote relay scopes must be an array.");
        }
        string[] scopes = scopesElement.EnumerateArray().Select(element =>
        {
            if (element.ValueKind != JsonValueKind.String || String.IsNullOrWhiteSpace(element.GetString()))
            {
                throw new InvalidOperationException("WinRemote relay scope is invalid.");
            }
            return element.GetString()!;
        }).ToArray();
        if (scopes.Length == 0 || scopes.Distinct(StringComparer.Ordinal).Count() != scopes.Length
            || scopes.Any(scope => !AllowedScopes.Contains(scope)))
        {
            throw new InvalidOperationException("WinRemote relay scopes are empty, duplicated, or unknown.");
        }
        return new RelaySecurityProfile(
            relayBaseUri,
            deviceId,
            fingerprint,
            clientId,
            secret,
            scopes,
            fullPath);
    }

    internal string ComputeAuthenticationMac(
        string challengeId,
        string nonce,
        IEnumerable<string> requestedScopes)
    {
        string[] scopes = requestedScopes.Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToArray();
        if (scopes.Length == 0 || scopes.Any(scope => !_scopes.Contains(scope, StringComparer.Ordinal)))
        {
            throw new InvalidOperationException("Requested relay scopes are not authorized by the selected profile.");
        }
        string canonical = String.Join("\n",
            AuthenticationProtocol,
            ClientId,
            challengeId,
            nonce,
            DeviceId,
            String.Join(',', scopes));
        byte[] mac = HMACSHA256.HashData(_clientSecret, Encoding.UTF8.GetBytes(canonical));
        return Convert.ToBase64String(mac).TrimEnd('=').Replace('+', '-').Replace('/', '_');
    }

    internal HttpClientHandler CreateHttpClientHandler() => new()
    {
        UseProxy = false,
        SslProtocols = SslProtocols.Tls12 | SslProtocols.Tls13,
        ServerCertificateCustomValidationCallback = ValidateServerCertificate,
    };

    internal void ConfigureWebSocket(ClientWebSocketOptions options)
    {
        options.Proxy = null;
        options.RemoteCertificateValidationCallback = ValidateServerCertificate;
    }

    internal bool ValidateServerCertificate(
        object sender,
        X509Certificate? certificate,
        X509Chain? chain,
        SslPolicyErrors errors)
    {
        _ = sender;
        _ = chain;
        if (certificate is null
            || (errors & (SslPolicyErrors.RemoteCertificateNameMismatch | SslPolicyErrors.RemoteCertificateNotAvailable)) != 0)
        {
            return false;
        }
        using X509Certificate2 certificate2 = new(certificate);
        byte[] actualFingerprint = SHA256.HashData(certificate2.RawData);
        if (!CryptographicOperations.FixedTimeEquals(actualFingerprint, _certificateSha256))
        {
            return false;
        }
        string commonName = certificate2.GetNameInfo(X509NameType.SimpleName, forIssuer: false);
        if (!String.Equals(commonName, "8ax-device-" + DeviceId, StringComparison.Ordinal))
        {
            return false;
        }
        DateTime now = DateTime.UtcNow;
        if (now < certificate2.NotBefore.ToUniversalTime() || now > certificate2.NotAfter.ToUniversalTime())
        {
            return false;
        }
        using ECDsa? publicKey = certificate2.GetECDsaPublicKey();
        return publicKey?.KeySize == 256;
    }

    private static string RequiredString(IReadOnlyDictionary<string, JsonElement> fields, string name)
    {
        JsonElement value = fields[name];
        if (value.ValueKind != JsonValueKind.String || String.IsNullOrWhiteSpace(value.GetString()))
        {
            throw new InvalidOperationException($"WinRemote relay security profile field {name} is invalid.");
        }
        return value.GetString()!;
    }

    private static byte[] DecodeHex(string value, int expectedBytes, string field)
    {
        if (value.Length != expectedBytes * 2 || value.Any(ch => !Uri.IsHexDigit(ch)))
        {
            throw new InvalidOperationException($"WinRemote relay {field} is invalid.");
        }
        return Convert.FromHexString(value);
    }
}
