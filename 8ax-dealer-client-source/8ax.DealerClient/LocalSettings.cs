using System.Text.Json;

namespace EightAxis.DealerClient;

internal sealed class LocalSettings
{
    public const string PrimaryServerUrl = "https://license.cjwsjzyy.xyz";

    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web)
    {
        WriteIndented = true
    };

    public string ServerUrl { get; set; } = PrimaryServerUrl;
    public string LastUsername { get; set; } = "";

    public static string SettingsPath
    {
        get
        {
            var dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "8ax", "DealerClient");
            Directory.CreateDirectory(dir);
            return Path.Combine(dir, "settings.json");
        }
    }

    public static LocalSettings Load()
    {
        try
        {
            if (!File.Exists(SettingsPath))
            {
                return new LocalSettings();
            }

            var text = File.ReadAllText(SettingsPath);
            var settings = JsonSerializer.Deserialize<LocalSettings>(text, Json) ?? new LocalSettings();
            if (!string.Equals(settings.ServerUrl, PrimaryServerUrl, StringComparison.OrdinalIgnoreCase))
            {
                settings.ServerUrl = PrimaryServerUrl;
                settings.Save();
            }

            return settings;
        }
        catch
        {
            return new LocalSettings();
        }
    }

    public void Save()
    {
        var text = JsonSerializer.Serialize(this, Json);
        File.WriteAllText(SettingsPath, text);
    }
}
