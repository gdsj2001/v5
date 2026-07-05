using System.Windows;
using System.Threading;

namespace EightAxis.WinRemote;

public partial class App : Application
{
    private const string SingleInstanceMutexName = @"Local\EightAxis.WinRemote.SingleInstance";
    private Mutex? _singleInstanceMutex;
    private bool _ownsSingleInstanceMutex;

    protected override void OnStartup(StartupEventArgs e)
    {
        _singleInstanceMutex = new Mutex(initiallyOwned: true, SingleInstanceMutexName, out bool ownsMutex);
        _ownsSingleInstanceMutex = ownsMutex;
        if (!_ownsSingleInstanceMutex)
        {
            MessageBox.Show(
                "8ax WinRemote 已经打开，只允许运行一个窗口。",
                "8ax WinRemote",
                MessageBoxButton.OK,
                MessageBoxImage.Information);
            Shutdown(0);
            return;
        }

        base.OnStartup(e);
    }

    protected override void OnExit(ExitEventArgs e)
    {
        if (_ownsSingleInstanceMutex)
        {
            _singleInstanceMutex?.ReleaseMutex();
        }
        _singleInstanceMutex?.Dispose();
        _singleInstanceMutex = null;
        _ownsSingleInstanceMutex = false;
        base.OnExit(e);
    }
}
