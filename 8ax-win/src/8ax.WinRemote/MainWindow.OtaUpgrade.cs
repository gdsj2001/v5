using System.Windows;
using EightAxis.WinRemote.Protocol;
using EightAxis.WinRemote.Transport;

namespace EightAxis.WinRemote;

public partial class MainWindow
{
    private async void OtaUpgradeButton_OnClick(object sender, RoutedEventArgs e)
    {
        OtaUpgradeButton.IsEnabled = false;
        OtaUpgradeButton.Content = "OTA\u8bf7\u6c42\u4e2d";
        try
        {
            _evidence.RecordEvent("ota_upgrade_requested", new Dictionary<string, object?>
            {
                ["relay"] = _settings.RelayBaseUri,
                ["endpoint"] = "/remote/ota/upgrade",
                ["scope_policy"] = "dna_private_first_no_public_when_private_present",
            });

            OtaUpgradeResult result = await RequestOtaUpgradeAsync();
            bool accepted = IsOtaAccepted(result.Status);
            _evidence.RecordEvent("ota_upgrade_response", new Dictionary<string, object?>
            {
                ["relay"] = _settings.RelayBaseUri,
                ["status"] = result.Status,
                ["code"] = result.Code,
                ["selected_scope"] = result.SelectedScope,
                ["job_id"] = result.JobId,
                ["cancellable"] = result.Cancellable,
            });

            StatusText.Text = $"OTA upgrade {result.Status}: {Compact(result.Code)} {Compact(result.Message)}  {RemotePointerStatusText}";
            MessageBox.Show(
                this,
                BuildOtaUpgradeMessage(result),
                "OTA\u5347\u7ea7",
                MessageBoxButton.OK,
                accepted ? MessageBoxImage.Information : MessageBoxImage.Warning);
        }
        catch (Exception ex) when (ex is not OperationCanceledException || !_shutdown.IsCancellationRequested)
        {
            _evidence.RecordEvent("ota_upgrade_failed", new Dictionary<string, object?>
            {
                ["relay"] = _settings.RelayBaseUri,
                ["endpoint"] = "/remote/ota/upgrade",
                ["message"] = ex.Message,
            });
            StatusText.Text = $"OTA upgrade failed: {Compact(ex.Message)}  {RemotePointerStatusText}";
            MessageBox.Show(this, Compact(ex.Message), "OTA\u5347\u7ea7", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
        finally
        {
            OtaUpgradeButton.Content = "OTA\u5347\u7ea7";
            OtaUpgradeButton.IsEnabled = true;
        }
    }

    private async Task<OtaUpgradeResult> RequestOtaUpgradeAsync()
    {
        if (_settings.RelayBaseUri is null)
        {
            throw new InvalidOperationException("Relay base URI is not configured.");
        }

        using RemoteRelayClient client = new(_settings.RelayBaseUri);
        return await client.RequestOtaUpgradeAsync(_shutdown.Token);
    }

    private static bool IsOtaAccepted(string status) =>
        String.Equals(status, "accepted", StringComparison.OrdinalIgnoreCase)
        || String.Equals(status, "queued", StringComparison.OrdinalIgnoreCase)
        || String.Equals(status, "running", StringComparison.OrdinalIgnoreCase);

    private static string BuildOtaUpgradeMessage(OtaUpgradeResult result)
    {
        string selectedScope = String.IsNullOrWhiteSpace(result.SelectedScope) ? "--" : result.SelectedScope;
        string jobId = String.IsNullOrWhiteSpace(result.JobId) ? "--" : result.JobId;
        string cancellable = result.Cancellable ? "yes" : "no";
        return
            $"status: {result.Status}\n" +
            $"code: {result.Code}\n" +
            $"selected_scope: {selectedScope}\n" +
            $"job_id: {jobId}\n" +
            $"cancellable: {cancellable}\n\n" +
            result.Message;
    }
}
