using System.Security.Cryptography;
using System.Text;

namespace EightAxis.DealerClient;

internal static class MachineFingerprint
{
    public static string CreateDigest()
    {
        var input = string.Join("|", new[]
        {
            Environment.MachineName,
            Environment.UserDomainName,
            Environment.OSVersion.VersionString,
            Environment.ProcessorCount.ToString(),
            Environment.GetFolderPath(Environment.SpecialFolder.Windows)
        });

        var hash = SHA256.HashData(Encoding.UTF8.GetBytes(input));
        return Convert.ToHexString(hash).ToLowerInvariant();
    }
}
