namespace EightAxis.FactoryClient;

internal sealed partial class MainForm : Form
{
    private readonly ApiClient _api = new();
    private readonly LocalSettings _settings = LocalSettings.Load();

    private TextBox _serverUrl = null!;
    private TextBox _adminUser = null!;
    private TextBox _adminPassword = null!;
    private TextBox _factoryAuthPrivateKey = null!;
    private ListView _dealers = null!;
    private ListView _dealerUsers = null!;
    private DataGridView _devices = null!;
    private ListView _upgradeRequests = null!;
    private TextBox _reviewNote = null!;
    private TextBox _deviceNote = null!;
    private TextBox _upgradeReviewNote = null!;
    private TextBox _requestInput = null!;
    private ListView _parsed = null!;
    private TextBox _log = null!;

    public MainForm()
    {
        Text = "8ax 厂家授权控制台";
        AutoScaleMode = AutoScaleMode.Dpi;
        AutoScroll = true;
        ClientSize = new Size(1600, 1200);
        MinimumSize = new Size(1180, 760);
        StartPosition = FormStartPosition.CenterScreen;
        Font = new Font("Microsoft YaHei UI", 9F, FontStyle.Regular, GraphicsUnit.Point);

        BuildUi();
        LoadSettings();
    }

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        _api.Dispose();
        base.OnFormClosed(e);
    }

}
