using System.Diagnostics;
using System.Reflection;

const string ScriptResourceName = "v5_git_nas_backup.ps1";
const string ScopeResourceName = "v5_git_scope.ps1";

static int Fail(string message)
{
    Console.ForegroundColor = ConsoleColor.Red;
    Console.WriteLine("ERROR: " + message);
    Console.ResetColor();
    WaitBeforeExit();
    return 1;
}

static void WaitBeforeExit()
{
    if (Environment.GetEnvironmentVariable("V5_GIT_NAS_BACKUP_NO_PAUSE") == "1")
    {
        return;
    }

    Console.WriteLine();
    Console.Write("Press any key to close...");
    Console.ReadKey(intercept: true);
    Console.WriteLine();
}

Console.OutputEncoding = System.Text.Encoding.UTF8;
Console.InputEncoding = System.Text.Encoding.UTF8;
Console.Title = "v5 Git + NAS Backup";

var assembly = Assembly.GetExecutingAssembly();
await using var scriptResource = assembly.GetManifestResourceStream(ScriptResourceName);
await using var scopeResource = assembly.GetManifestResourceStream(ScopeResourceName);
if (scriptResource is null || scopeResource is null)
{
    return Fail("Embedded backup scripts were not found.");
}

var tempDir = Path.Combine(Path.GetTempPath(), "v5_git_nas_backup");
Directory.CreateDirectory(tempDir);
var runDir = Path.Combine(tempDir, Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(runDir);
var tempScript = Path.Combine(runDir, "v5_git_nas_backup.ps1");
var tempScope = Path.Combine(runDir, "v5_git_scope.ps1");

try
{
    await using (var output = File.Create(tempScript))
    {
        await scriptResource.CopyToAsync(output);
    }
    await using (var output = File.Create(tempScope))
    {
        await scopeResource.CopyToAsync(output);
    }

    var psi = new ProcessStartInfo
    {
        FileName = "powershell.exe",
        UseShellExecute = false,
        RedirectStandardOutput = false,
        RedirectStandardError = false,
    };
    psi.ArgumentList.Add("-NoProfile");
    psi.ArgumentList.Add("-ExecutionPolicy");
    psi.ArgumentList.Add("Bypass");
    psi.ArgumentList.Add("-File");
    psi.ArgumentList.Add(tempScript);
    foreach (var arg in args)
    {
        psi.ArgumentList.Add(arg);
    }

    using var process = Process.Start(psi);
    if (process is null)
    {
        return Fail("Could not start powershell.exe.");
    }

    await process.WaitForExitAsync();
    var exitCode = process.ExitCode;

    Console.WriteLine();
    if (exitCode == 0)
    {
        Console.ForegroundColor = ConsoleColor.Green;
        Console.WriteLine("Git push and backup finished.");
        Console.ResetColor();
    }
    else
    {
        Console.ForegroundColor = ConsoleColor.Red;
        Console.WriteLine($"Git push or backup failed with exit code {exitCode}.");
        Console.ResetColor();
    }

    WaitBeforeExit();
    return exitCode;
}
catch (Exception ex)
{
    return Fail(ex.Message);
}
finally
{
    try
    {
        if (File.Exists(tempScript))
        {
            File.Delete(tempScript);
        }
        if (File.Exists(tempScope))
        {
            File.Delete(tempScope);
        }
        if (Directory.Exists(runDir))
        {
            Directory.Delete(runDir);
        }
    }
    catch
    {
        // Best-effort cleanup only.
    }
}
