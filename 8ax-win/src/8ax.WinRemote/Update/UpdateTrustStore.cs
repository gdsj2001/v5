using System.IO;
using System.Text;
using System.Text.Json;

namespace EightAxis.WinRemote.Update;

public sealed record TrustedUpdateRecord(
    string Schema,
    string KeyId,
    long ReleaseSequence,
    string Version,
    string PackageSha256);

public sealed class UpdateTrustStore
{
    public const string Schema = "v5.winremote_update_trust.v1";
    private static readonly UTF8Encoding Utf8NoBom = new(false, true);
    private readonly string _path;

    public UpdateTrustStore(string? path = null)
    {
        _path = path ?? System.IO.Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "8ax", "WinRemote", "trusted-update.json");
    }

    public string Path => _path;

    public TrustedUpdateRecord? Read()
    {
        if (!File.Exists(_path))
        {
            return null;
        }
        FileInfo info = new(_path);
        if ((info.Attributes & FileAttributes.ReparsePoint) != 0 || info.Length <= 0 || info.Length > 16 * 1024)
        {
            throw new InvalidOperationException("Trusted update record file identity is invalid.");
        }
        byte[] raw = File.ReadAllBytes(_path);
        using JsonDocument document = JsonDocument.Parse(raw, new JsonDocumentOptions
        {
            AllowTrailingCommas = false,
            CommentHandling = JsonCommentHandling.Disallow,
            MaxDepth = 4,
        });
        JsonElement root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidOperationException("Trusted update record root is invalid.");
        }
        Dictionary<string, JsonElement> fields = new(StringComparer.Ordinal);
        foreach (JsonProperty property in root.EnumerateObject())
        {
            if (!fields.TryAdd(property.Name, property.Value))
            {
                throw new InvalidOperationException("Trusted update record contains duplicate fields.");
            }
        }
        string[] expected = ["schema", "key_id", "release_sequence", "version", "package_sha256"];
        if (fields.Count != expected.Length || expected.Any(name => !fields.ContainsKey(name)))
        {
            throw new InvalidOperationException("Trusted update record fields are invalid.");
        }
        TrustedUpdateRecord record = new(
            RequiredString(fields, "schema"),
            RequiredString(fields, "key_id"),
            RequiredInt64(fields, "release_sequence"),
            RequiredString(fields, "version"),
            RequiredString(fields, "package_sha256").ToLowerInvariant());
        if (record.Schema != Schema || record.KeyId != UpdateManifestVerifier.SigningKeyId ||
            record.ReleaseSequence <= 0 || record.PackageSha256.Length != 64 ||
            record.PackageSha256.Any(ch => !Uri.IsHexDigit(ch)))
        {
            throw new InvalidOperationException("Trusted update record values are invalid.");
        }
        return record;
    }

    public void ValidateNoRollback(UpdateManifest manifest)
    {
        TrustedUpdateRecord? record = Read();
        if (record is null)
        {
            return;
        }
        if (manifest.ReleaseSequence < record.ReleaseSequence)
        {
            throw new InvalidOperationException(
                $"Update release_sequence rollback rejected: {manifest.ReleaseSequence} < {record.ReleaseSequence}.");
        }
        if (manifest.ReleaseSequence == record.ReleaseSequence &&
            (manifest.KeyId != record.KeyId || manifest.Version != record.Version ||
             !String.Equals(manifest.Sha256, record.PackageSha256, StringComparison.OrdinalIgnoreCase)))
        {
            throw new InvalidOperationException("Update release_sequence identity conflict rejected.");
        }
    }

    public void RecordInstalled(UpdateManifest manifest)
    {
        ValidateNoRollback(manifest);
        string? directory = System.IO.Path.GetDirectoryName(_path);
        if (String.IsNullOrWhiteSpace(directory))
        {
            throw new InvalidOperationException("Trusted update record directory is invalid.");
        }
        Directory.CreateDirectory(directory);
        if ((File.GetAttributes(directory) & FileAttributes.ReparsePoint) != 0)
        {
            throw new InvalidOperationException("Trusted update record directory must not be a reparse point.");
        }
        byte[] raw = Utf8NoBom.GetBytes(JsonSerializer.Serialize(new Dictionary<string, object>
        {
            ["schema"] = Schema,
            ["key_id"] = manifest.KeyId,
            ["release_sequence"] = manifest.ReleaseSequence,
            ["version"] = manifest.Version,
            ["package_sha256"] = manifest.Sha256.ToLowerInvariant(),
        }));
        string temporary = _path + ".tmp-" + Guid.NewGuid().ToString("N");
        try
        {
            using (FileStream stream = new(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None,
                       4096, FileOptions.WriteThrough))
            {
                stream.Write(raw);
                stream.Flush(flushToDisk: true);
            }
            File.Move(temporary, _path, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporary))
            {
                File.Delete(temporary);
            }
        }
    }

    private static string RequiredString(IReadOnlyDictionary<string, JsonElement> fields, string name)
    {
        JsonElement element = fields[name];
        if (element.ValueKind != JsonValueKind.String || String.IsNullOrWhiteSpace(element.GetString()))
        {
            throw new InvalidOperationException($"Trusted update record field {name} is invalid.");
        }
        return element.GetString()!;
    }

    private static long RequiredInt64(IReadOnlyDictionary<string, JsonElement> fields, string name)
    {
        JsonElement element = fields[name];
        if (element.ValueKind != JsonValueKind.Number || !element.TryGetInt64(out long value))
        {
            throw new InvalidOperationException($"Trusted update record field {name} is invalid.");
        }
        return value;
    }
}
