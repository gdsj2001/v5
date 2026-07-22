using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace EightAxis.WinRemote.Update;

public static class UpdateManifestVerifier
{
    public const string ManifestSchema = "v5.winremote_update_manifest.v2";
    public const string SigningKeyId = "winremote-update-p256-2026-01";
    public const string ReleaseChannel = "stable";
    public const string PublicKeyResourceName = "EightAxis.WinRemote.Update.winremote-update-signing-public.pem";
    public const int MaxManifestBytes = 64 * 1024;
    public const int MaxSignatureBytes = 256;
    public const long MaxPackageBytes = 2L * 1024 * 1024 * 1024;

    private static readonly UTF8Encoding StrictUtf8 = new(false, true);
    private static readonly Regex HexSha256 = new("^[0-9a-fA-F]{64}$", RegexOptions.CultureInvariant);
    private static readonly string[] RequiredFields =
    [
        "schema", "app_id", "channel", "version", "release_sequence", "key_id",
        "file_name", "package_url", "size", "sha256", "published_at_utc",
    ];

    public static UpdateManifest VerifyAndParse(ReadOnlySpan<byte> manifestBytes, ReadOnlySpan<byte> signatureBytes)
    {
        using ECDsa publicKey = LoadPublicKey();
        return VerifyAndParse(manifestBytes, signatureBytes, publicKey);
    }

    public static UpdateManifest VerifyAndParse(
        ReadOnlySpan<byte> manifestBytes,
        ReadOnlySpan<byte> signatureBytes,
        ECDsa trustedPublicKey)
    {
        ArgumentNullException.ThrowIfNull(trustedPublicKey);
        if (manifestBytes.IsEmpty || manifestBytes.Length > MaxManifestBytes)
        {
            throw new InvalidOperationException("Update manifest byte length is invalid.");
        }
        if (signatureBytes.IsEmpty || signatureBytes.Length > MaxSignatureBytes)
        {
            throw new InvalidOperationException("Update manifest signature byte length is invalid.");
        }

        if (trustedPublicKey.KeySize != 256 || !trustedPublicKey.VerifyData(
            manifestBytes,
            signatureBytes,
            HashAlgorithmName.SHA256,
            DSASignatureFormat.Rfc3279DerSequence))
        {
            throw new CryptographicException("Update manifest signature is invalid.");
        }

        if (manifestBytes.Length >= 3 && manifestBytes[..3].SequenceEqual(new byte[] { 0xEF, 0xBB, 0xBF }))
        {
            throw new InvalidOperationException("Update manifest must be UTF-8 without BOM.");
        }
        string json;
        try
        {
            json = StrictUtf8.GetString(manifestBytes);
        }
        catch (DecoderFallbackException ex)
        {
            throw new InvalidOperationException("Update manifest is not strict UTF-8.", ex);
        }

        using JsonDocument document = JsonDocument.Parse(json, new JsonDocumentOptions
        {
            AllowTrailingCommas = false,
            CommentHandling = JsonCommentHandling.Disallow,
            MaxDepth = 8,
        });
        JsonElement root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidOperationException("Update manifest root must be an object.");
        }
        Dictionary<string, JsonElement> fields = new(StringComparer.Ordinal);
        foreach (JsonProperty property in root.EnumerateObject())
        {
            if (!fields.TryAdd(property.Name, property.Value))
            {
                throw new InvalidOperationException($"Update manifest duplicates field {property.Name}.");
            }
        }
        if (fields.Count != RequiredFields.Length || RequiredFields.Any(name => !fields.ContainsKey(name)))
        {
            throw new InvalidOperationException("Update manifest fields are invalid.");
        }

        UpdateManifest manifest = new()
        {
            Schema = RequiredString(fields, "schema"),
            AppId = RequiredString(fields, "app_id"),
            Channel = RequiredString(fields, "channel"),
            Version = RequiredString(fields, "version"),
            ReleaseSequence = RequiredInt64(fields, "release_sequence"),
            KeyId = RequiredString(fields, "key_id"),
            FileName = RequiredString(fields, "file_name"),
            PackageUrl = RequiredString(fields, "package_url"),
            Size = RequiredInt64(fields, "size"),
            Sha256 = RequiredString(fields, "sha256").ToLowerInvariant(),
            PublishedAtUtc = RequiredString(fields, "published_at_utc"),
        };
        Validate(manifest);
        return manifest;
    }

    public static FileStream OpenExclusiveVerifiedPackage(string path, UpdateManifest manifest)
    {
        FileStream stream = new(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.None,
            64 * 1024,
            FileOptions.SequentialScan);
        try
        {
            VerifyPackage(stream, manifest);
            stream.Position = 0;
            return stream;
        }
        catch
        {
            stream.Dispose();
            throw;
        }
    }

    public static string VerifyPackage(Stream stream, UpdateManifest manifest)
    {
        if (!stream.CanRead || !stream.CanSeek)
        {
            throw new InvalidOperationException("Update package stream must be readable and seekable.");
        }
        if (stream.Length != manifest.Size)
        {
            throw new InvalidOperationException($"Update package size mismatch: {stream.Length} != {manifest.Size}.");
        }
        stream.Position = 0;
        byte[] actual = SHA256.HashData(stream);
        byte[] expected = Convert.FromHexString(manifest.Sha256);
        if (!CryptographicOperations.FixedTimeEquals(actual, expected))
        {
            throw new CryptographicException("Update package SHA-256 mismatch.");
        }
        stream.Position = 0;
        return Convert.ToHexString(actual).ToLowerInvariant();
    }

    private static ECDsa LoadPublicKey()
    {
        using Stream stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(PublicKeyResourceName)
            ?? throw new InvalidOperationException("Embedded WinRemote update signing public key is missing.");
        using StreamReader reader = new(stream, StrictUtf8, detectEncodingFromByteOrderMarks: false);
        string pem = reader.ReadToEnd();
        ECDsa key = ECDsa.Create();
        key.ImportFromPem(pem);
        if (key.KeySize != 256)
        {
            key.Dispose();
            throw new InvalidOperationException("WinRemote update signing public key is not P-256.");
        }
        return key;
    }

    private static void Validate(UpdateManifest manifest)
    {
        if (manifest.Schema != ManifestSchema || manifest.AppId != WinRemoteUpdater.AppId)
        {
            throw new InvalidOperationException("Update manifest schema/app identity is invalid.");
        }
        if (manifest.Channel != ReleaseChannel || manifest.KeyId != SigningKeyId)
        {
            throw new InvalidOperationException("Update manifest channel/signing key identity is invalid.");
        }
        if (manifest.ReleaseSequence <= 0 || manifest.Size <= 0 || manifest.Size > MaxPackageBytes)
        {
            throw new InvalidOperationException("Update manifest release sequence/package size is invalid.");
        }
        if (manifest.Version.Length > 64 || !manifest.Version.Any(Char.IsDigit))
        {
            throw new InvalidOperationException("Update manifest version is invalid.");
        }
        if (manifest.FileName != Path.GetFileName(manifest.FileName) || !manifest.FileName.EndsWith(".zip", StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException("Update manifest file_name must be one zip basename.");
        }
        if (String.IsNullOrWhiteSpace(manifest.PackageUrl) || !HexSha256.IsMatch(manifest.Sha256))
        {
            throw new InvalidOperationException("Update manifest package_url/SHA-256 is invalid.");
        }
        if (!DateTimeOffset.TryParse(manifest.PublishedAtUtc, out DateTimeOffset published) || published.Offset != TimeSpan.Zero)
        {
            throw new InvalidOperationException("Update manifest published_at_utc is invalid.");
        }
    }

    private static string RequiredString(IReadOnlyDictionary<string, JsonElement> fields, string name)
    {
        JsonElement value = fields[name];
        if (value.ValueKind != JsonValueKind.String || String.IsNullOrWhiteSpace(value.GetString()))
        {
            throw new InvalidOperationException($"Update manifest field {name} is invalid.");
        }
        return value.GetString()!;
    }

    private static long RequiredInt64(IReadOnlyDictionary<string, JsonElement> fields, string name)
    {
        JsonElement value = fields[name];
        if (value.ValueKind != JsonValueKind.Number || !value.TryGetInt64(out long parsed))
        {
            throw new InvalidOperationException($"Update manifest field {name} is invalid.");
        }
        return parsed;
    }
}
