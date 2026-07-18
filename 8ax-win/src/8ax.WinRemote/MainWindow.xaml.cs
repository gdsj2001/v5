using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;
using EightAxis.WinRemote.Config;
using EightAxis.WinRemote.Diagnostics;
using EightAxis.WinRemote.Input;
using EightAxis.WinRemote.Protocol;
using EightAxis.WinRemote.Rendering;
using EightAxis.WinRemote.Transport;
using EightAxis.WinRemote.Update;
using Microsoft.Win32;
using System.IO;
using System.Security.Cryptography;

namespace EightAxis.WinRemote;

public partial class MainWindow : Window
{
    private const double RelayMoveMinIntervalMs = 16.0;
    private const int RelayMoveMinPixelDelta = 1;
    private const int RelayReconnectInitialDelayMs = 700;
    private const int RelayReconnectMaxDelayMs = 5000;
    private const int SystemMetricsRefreshMs = 2000;
    private const int RelayStreamTargetFps = 30;
    private const double RelayFrameMetricsMinIntervalMs = 1000.0 / RelayStreamTargetFps;

    private readonly AppSettings _settings = AppSettings.LoadFromEnvironment();
    private readonly RemoteFramebuffer _relayFramebuffer = new();
    private readonly RemoteFrameAssembler _relayAssembler;
    private readonly FrameStats _stats = new();
    private readonly RuntimeEvidenceRecorder _evidence;
    private readonly WinRemoteUpdater _updater = new();
    private readonly CancellationTokenSource _shutdown = new();
    private readonly DispatcherTimer _systemMetricsTimer = new();
    private readonly string _relaySessionId = $"win-{Guid.NewGuid():N}";
    private RemoteRelayClient? _activeRelayClient;
    private bool _relayInputReady;
    private bool _relaySessionConnected;
    private bool _updateAvailable;
    private bool _awaitingRelayPointerFeedback;
    private bool _relayPointerIsDown;
    private bool _haveLastRelayPoint;
    private RemotePoint _lastRelayPoint;
    private bool _haveLastRelayMovePoint;
    private RemotePoint _lastRelayMovePoint;
    private DateTime _lastRelayFrameMetricsUtc = DateTime.MinValue;
    private string _connectionStateText = String.Empty;
    private string _connectionStateBackground = String.Empty;
    private string _connectionStateForeground = String.Empty;
    private long _relayPointerSeq;
    private int _relayMoveSendActive;
    private int _relayInputEnsureActive;
    private int _systemMetricsFetchActive;
    private DateTime _lastRelayPointerDownUtc;
    private DateTime _lastRelayMoveUtc = DateTime.MinValue;
    private bool _pointerCaptured;

    public MainWindow()
    {
        InitializeComponent();
        _evidence = new RuntimeEvidenceRecorder(_settings.EvidenceDirectory);
        _relayAssembler = new RemoteFrameAssembler(_relayFramebuffer);
        _evidence.RecordEvent("app_start", new Dictionary<string, object?>
        {
            ["mode"] = _settings.SourceMode.ToString(),
            ["evidence_dir"] = _settings.EvidenceDirectory,
            ["view_only"] = _settings.ViewOnly,
            ["enable_pointer"] = _settings.EnablePointer,
            ["enable_remote_input"] = _settings.EnableRemoteInput,
        });
        InputModeText.Text = PointerInputModeText;

        if (_settings.SourceMode != RemoteSourceMode.Relay || _settings.RelayBaseUri is null)
        {
            throw new InvalidOperationException("WinRemote requires relay stream mode; direct board framebuffer capture is retired.");
        }

        SourceText.Text = "relay stream";
        EmptyStateText.Text = $"Waiting for relay {_settings.RelayBaseUri}...";
        RemoteImage.Source = _relayFramebuffer.Bitmap;
        StatusText.Text = $"source: {_settings.RelayBaseUri}";
        UpdateRemotePointerStatusSlot();
        _evidence.RecordEvent("relay_mode_selected", new Dictionary<string, object?>
        {
            ["relay"] = _settings.RelayBaseUri,
            ["enable_remote_input"] = _settings.EnableRemoteInput,
        });
        _systemMetricsTimer.Interval = TimeSpan.FromMilliseconds(SystemMetricsRefreshMs);
        _systemMetricsTimer.Tick += async (_, _) => await RefreshSystemMetricsAsync();
        _systemMetricsTimer.Start();
        _ = RunRelayLoopAsync(_settings.RelayBaseUri);
        _ = CheckForVpsUpdateAsync();
    }

    protected override void OnClosed(EventArgs e)
    {
        _shutdown.Cancel();
        _systemMetricsTimer.Stop();
        _evidence.RecordEvent("app_closed");
        _updater.Dispose();
        _shutdown.Dispose();
        base.OnClosed(e);
    }
}
