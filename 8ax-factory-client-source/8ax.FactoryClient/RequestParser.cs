using System.Text.Json;
using System.Text.RegularExpressions;

namespace EightAxis.FactoryClient;

internal static partial class RequestParser
{
    public static ParsedRequest Parse(string raw)
    {
        raw = raw.Trim();
        if (string.IsNullOrWhiteSpace(raw))
        {
            return new ParsedRequest("empty", null, null, null, null, null, null, null, "");
        }

        if (TryParseJson(raw, out var parsed))
        {
            return parsed;
        }

        return new ParsedRequest(
            "text",
            FindValue(raw, "device_id", "deviceId", "设备", "设备ID"),
            FindValue(raw, "upgrade_request_id", "upgradeRequestId", "request_id"),
            FindValue(raw, "dealer_id", "dealerId", "经销商"),
            FindValue(raw, "dealer_daily_code", "dailyCode", "每日码", "校验码"),
            FindValue(raw, "current_version", "currentVersion", "当前版本"),
            FindValue(raw, "target_version", "targetVersion", "目标版本"),
            FindValue(raw, "target_capabilities", "capabilities", "升级内容", "目标能力"),
            raw);
    }

    private static bool TryParseJson(string raw, out ParsedRequest parsed)
    {
        parsed = new ParsedRequest("json", null, null, null, null, null, null, null, raw);
        try
        {
            using var doc = JsonDocument.Parse(raw);
            var root = doc.RootElement;
            parsed = new ParsedRequest(
                "json",
                GetString(root, "device_id", "deviceId"),
                GetString(root, "upgrade_request_id", "upgradeRequestId", "request_id"),
                GetString(root, "dealer_id", "dealerId"),
                GetString(root, "dealer_daily_code", "dailyCode", "dealerDailyCode"),
                GetString(root, "current_version", "currentVersion"),
                GetString(root, "target_version", "targetVersion"),
                GetString(root, "target_capabilities", "capabilities", "targetCapabilities"),
                raw);
            return true;
        }
        catch (JsonException)
        {
            return false;
        }
    }

    private static string? GetString(JsonElement root, params string[] names)
    {
        foreach (var name in names)
        {
            if (root.TryGetProperty(name, out var value))
            {
                return value.ValueKind == JsonValueKind.String ? value.GetString() : value.ToString();
            }
        }
        return null;
    }

    private static string? FindValue(string raw, params string[] keys)
    {
        foreach (var key in keys)
        {
            var match = Regex.Match(raw, $@"{Regex.Escape(key)}\s*[:=：]\s*([A-Za-z0-9_.@\-]+)", RegexOptions.IgnoreCase);
            if (match.Success)
            {
                return match.Groups[1].Value;
            }
        }
        return null;
    }
}
