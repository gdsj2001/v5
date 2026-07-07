using System.Text.Json;

namespace EightAxis.FactoryClient;

internal sealed class LocalSettings
{
    public const string PrimaryServerUrl = "https://license.cjwsjzyy.xyz";
    public const string DefaultAdminPassword = "sj";

    public string ServerUrl { get; set; } = PrimaryServerUrl;
    public string AdminUsername { get; set; } = "admin";
    public string FactoryDeviceAuthPrivateKeyPath { get; set; } = DefaultFactoryDeviceAuthPrivateKeyPath;

    public static string DefaultFactoryDeviceAuthPrivateKeyPath
    {
        get
        {
            return Path.Combine(
                "D:\\",
                "授权私钥",
                "factory-device-auth-private.pem");
        }
    }

    private static string SettingsPath
    {
        get
        {
            var dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "8ax", "FactoryClient");
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

            var value = JsonSerializer.Deserialize<LocalSettings>(File.ReadAllText(SettingsPath));
            if (value is null)
            {
                return new LocalSettings();
            }

            if (string.IsNullOrWhiteSpace(value.ServerUrl))
            {
                value.ServerUrl = PrimaryServerUrl;
            }
            if (string.IsNullOrWhiteSpace(value.FactoryDeviceAuthPrivateKeyPath))
            {
                value.FactoryDeviceAuthPrivateKeyPath = DefaultFactoryDeviceAuthPrivateKeyPath;
            }
            return value;
        }
        catch
        {
            return new LocalSettings();
        }
    }

    public void Save()
    {
        File.WriteAllText(SettingsPath, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
    }
}
