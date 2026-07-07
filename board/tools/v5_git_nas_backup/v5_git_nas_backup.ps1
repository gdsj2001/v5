param(
    [string]$RepoRoot = "",
    [string]$NasHost = "sjnas",
    [string]$NasShareName = "",
    [string]$GitRemote = "origin",
    [string]$GitBranch = "main",
    [int]$NasProbeTimeoutSeconds = 12,
    [switch]$NasDryRun,
    [switch]$GitDryRun
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

if ([string]::IsNullOrWhiteSpace($NasShareName)) {
    $NasShareName = -join ([char[]](0x5907, 0x4EFD))
}

if (-not [string]::IsNullOrWhiteSpace($env:V5_NAS_HOST)) {
    $NasHost = $env:V5_NAS_HOST
}
if (-not [string]::IsNullOrWhiteSpace($env:V5_NAS_SHARE)) {
    $NasShareName = $env:V5_NAS_SHARE
}

if ($env:V5_GIT_NAS_BACKUP_GIT_DRY_RUN -eq "1") {
    $GitDryRun = $true
}
if ($env:V5_GIT_NAS_BACKUP_NAS_DRY_RUN -eq "1") {
    $NasDryRun = $true
}

function Fail([string]$Message) {
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

function Get-FullDirectoryPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Test-V5RepoRoot([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $false
    }
    try {
        $full = Get-FullDirectoryPath $Path
    }
    catch {
        return $false
    }
    return (
        (Test-Path -LiteralPath $full -PathType Container) -and
        (Test-Path -LiteralPath (Join-Path $full "AGENTS.md") -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $full "board") -PathType Container) -and
        (Test-Path -LiteralPath (Join-Path $full ".git") -PathType Container)
    )
}

function Add-Candidate([System.Collections.Generic.List[string]]$Candidates, [string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return
    }
    try {
        $full = Get-FullDirectoryPath $Path
        if (-not $Candidates.Contains($full)) {
            $Candidates.Add($full) | Out-Null
        }
    }
    catch {
    }
}

function Add-Ancestors([System.Collections.Generic.List[string]]$Candidates, [string]$StartPath) {
    if ([string]::IsNullOrWhiteSpace($StartPath)) {
        return
    }
    try {
        $item = Get-Item -LiteralPath $StartPath -Force
        $dir = if ($item.PSIsContainer) { $item } else { $item.Directory }
        while ($null -ne $dir) {
            Add-Candidate $Candidates $dir.FullName
            $dir = $dir.Parent
        }
    }
    catch {
    }
}

function Resolve-V5RepoRoot([string]$ExplicitRoot) {
    $candidates = New-Object System.Collections.Generic.List[string]
    Add-Candidate $candidates $ExplicitRoot
    Add-Candidate $candidates $env:V5_REPO_ROOT
    Add-Ancestors $candidates (Get-Location).Path
    Add-Ancestors $candidates $PSScriptRoot

    Get-PSDrive -PSProvider FileSystem | ForEach-Object {
        Add-Candidate $candidates (Join-Path $_.Root "v5")
    }

    foreach ($candidate in $candidates) {
        if (Test-V5RepoRoot $candidate) {
            return Get-FullDirectoryPath $candidate
        }
    }

    Fail "Could not locate the v5 repository. Run from the repo, pass -RepoRoot, or set V5_REPO_ROOT."
}

function Invoke-WithTimeout([string]$Name, [int]$TimeoutSeconds, [scriptblock]$ScriptBlock, [object[]]$ArgumentList) {
    $job = Start-Job -ScriptBlock $ScriptBlock -ArgumentList $ArgumentList
    try {
        if (-not (Wait-Job $job -Timeout $TimeoutSeconds)) {
            Stop-Job $job
            Fail "$Name timed out after $TimeoutSeconds seconds."
        }
        $result = Receive-Job $job -ErrorAction Stop
        if ($job.State -eq "Failed") {
            $reason = $job.ChildJobs[0].JobStateInfo.Reason
            if ($null -ne $reason) {
                Fail "$Name failed: $($reason.Message)"
            }
            Fail "$Name failed."
        }
        return $result
    }
    finally {
        Remove-Job $job -Force -ErrorAction SilentlyContinue
    }
}

function Quote-ProcessArgument([string]$Argument) {
    if ($null -eq $Argument) {
        return '""'
    }
    if ($Argument -notmatch '[\s"]') {
        return $Argument
    }
    $escaped = $Argument -replace '(\\*)"', '$1$1\"'
    $escaped = $escaped -replace '(\\+)$', '$1$1'
    return '"' + $escaped + '"'
}

function Invoke-ProcessChecked([string]$FileName, [string[]]$Arguments, [string]$WorkingDirectory, [switch]$AllowFailure) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FileName
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.Arguments = ($Arguments | ForEach-Object { Quote-ProcessArgument $_ }) -join " "

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()

    if (-not [string]::IsNullOrWhiteSpace($stdout)) {
        Write-Host ($stdout.TrimEnd())
    }
    if (-not [string]::IsNullOrWhiteSpace($stderr)) {
        Write-Host ($stderr.TrimEnd())
    }

    if ($process.ExitCode -ne 0 -and -not $AllowFailure) {
        Fail "$FileName $($Arguments -join ' ') failed with exit code $($process.ExitCode)."
    }
    return $process.ExitCode
}

function Invoke-GitChecked([string]$Repo, [string[]]$GitArgs, [switch]$AllowFailure) {
    return Invoke-ProcessChecked -FileName "git.exe" -Arguments (@("-C", $Repo) + $GitArgs) -WorkingDirectory $Repo -AllowFailure:$AllowFailure
}

function Get-GitOutput([string]$Repo, [string[]]$GitArgs) {
    $output = & git -C $Repo @GitArgs 2>$null
    if ($LASTEXITCODE -ne 0) {
        return ""
    }
    return ($output -join "`n").Trim()
}

function Ensure-GitIdentity([string]$Repo) {
    $name = Get-GitOutput $Repo @("config", "--get", "user.name")
    if ([string]::IsNullOrWhiteSpace($name)) {
        Invoke-GitChecked -Repo $Repo -GitArgs @("config", "user.name", "v5-sync") | Out-Null
    }
    $email = Get-GitOutput $Repo @("config", "--get", "user.email")
    if ([string]::IsNullOrWhiteSpace($email)) {
        Invoke-GitChecked -Repo $Repo -GitArgs @("config", "user.email", "v5-sync@local") | Out-Null
    }
}

function Ensure-GitRepository([string]$Repo, [string]$Remote, [string]$Branch) {
    $currentBranch = Get-GitOutput $Repo @("branch", "--show-current")
    if ($currentBranch -ne $Branch) {
        Fail "Refusing to push branch '$currentBranch'. Expected '$Branch'."
    }

    $remoteUrl = Get-GitOutput $Repo @("remote", "get-url", $Remote)
    if ([string]::IsNullOrWhiteSpace($remoteUrl)) {
        Fail "Git remote '$Remote' is not configured."
    }

    Ensure-GitIdentity $Repo
}

function Get-RobocopyExcludedDirectories {
    return @(
        ".git",
        "repo_ignored",
        ".pytest_cache",
        ".mypy_cache",
        ".ruff_cache",
        "__pycache__",
        "node_modules",
        ".npm",
        ".pnpm-store",
        ".vs",
        "CMakeFiles",
        "bin",
        "obj",
        "build",
        "build-*",
        "cmake-build-*",
        "out",
        "dist",
        "tmp",
        "temp",
        "logs",
        "htmlcov",
        ".Xil",
        "*.cache",
        "*.gen",
        "*.hw",
        "*.runs",
        "artifacts",
        "evidence",
        "evidence_*",
        "*_evidence",
        "*evidence*",
        "Temporary",
        "release"
    )
}

function Get-RobocopyExcludedFiles {
    return @(
        ".DS_Store",
        "Thumbs.db",
        "Desktop.ini",
        "*~",
        "*.swp",
        "*.swo",
        "*.tmp",
        "*.temp",
        "*.orig",
        "*.pyc",
        "*.pyo",
        ".coverage",
        "CMakeCache.txt",
        "cmake_install.cmake",
        "compile_commands.json",
        "*.jou",
        "*.str",
        "*.log",
        "*.wdb",
        "*.vcd",
        "*.zip",
        "*.7z",
        "*.rar",
        "*.tar",
        "*.tar.gz",
        "*.tgz",
        "*.tar.bz2",
        "*.tbz2",
        "*.tar.xz",
        "*.txz",
        "*.gz",
        "*.bz2",
        "*.xz",
        "*.zst",
        "*.deb",
        "*.rpm",
        "*.msi",
        "*.dmg",
        "*.pkg",
        "*.AppImage",
        "*.iso",
        "*.raw",
        "*.frame",
        "*.jsonl",
        "*frame*.json",
        "*metrics*.json",
        "*events*.json",
        "*summary*.json"
    )
}

function Register-NasCredentialIfProvided([string]$HostName) {
    $user = $env:V5_NAS_USER
    $password = $env:V5_NAS_PASSWORD
    if ([string]::IsNullOrWhiteSpace($user) -and [string]::IsNullOrWhiteSpace($password)) {
        return
    }
    if ([string]::IsNullOrWhiteSpace($user) -or [string]::IsNullOrWhiteSpace($password)) {
        Fail "Set both V5_NAS_USER and V5_NAS_PASSWORD, or neither."
    }
    $cmdkey = (Get-Command cmdkey.exe -ErrorAction SilentlyContinue).Source
    if (-not $cmdkey) {
        Fail "cmdkey.exe was not found."
    }
    & $cmdkey "/add:$HostName" "/user:$user" "/pass:$password" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Fail "Could not register NAS credentials for $HostName."
    }
}

function Ensure-NasTarget([string]$ShareRoot, [string]$TargetRoot, [int]$TimeoutSeconds) {
    Write-Host "Checking NAS share responsiveness..."
    $shareExists = Invoke-WithTimeout -Name "NAS share check" -TimeoutSeconds $TimeoutSeconds -ScriptBlock {
        param($Path)
        Test-Path -LiteralPath $Path
    } -ArgumentList @($ShareRoot)
    if (-not $shareExists) {
        Fail "NAS share is not accessible: $ShareRoot"
    }

    Write-Host "Checking NAS target directory..."
    Invoke-WithTimeout -Name "NAS target create/check" -TimeoutSeconds $TimeoutSeconds -ScriptBlock {
        param($Path)
        New-Item -ItemType Directory -Force -Path $Path | Out-Null
        Test-Path -LiteralPath $Path -PathType Container
    } -ArgumentList @($TargetRoot) | Out-Null
}

function Invoke-NasMirrorBackup([string]$SourceRoot, [string]$HostName, [string]$ShareName, [int]$TimeoutSeconds, [switch]$DryRun) {
    $shareRoot = "\\$HostName\$ShareName"
    $targetRoot = Join-Path $shareRoot "v5"

    Write-Host ""
    Write-Host "=== NAS backup ==="
    Write-Host "Source: $SourceRoot"
    Write-Host "Target: $targetRoot"
    if ($DryRun) {
        Write-Host "Mode:   NAS dry-run, robocopy will list without changing files"
    }

    $robocopy = (Get-Command robocopy.exe -ErrorAction SilentlyContinue).Source
    if (-not $robocopy) {
        Fail "robocopy.exe was not found."
    }

    Register-NasCredentialIfProvided $HostName
    Ensure-NasTarget $shareRoot $targetRoot $TimeoutSeconds

    $args = @(
        $SourceRoot,
        $targetRoot,
        "/MIR",
        "/COPY:DAT",
        "/DCOPY:DAT",
        "/FFT",
        "/R:2",
        "/W:2",
        "/NP",
        "/NFL",
        "/NDL",
        "/NJH",
        "/A-:R",
        "/XD"
    ) + (Get-RobocopyExcludedDirectories) + @("/XF") + (Get-RobocopyExcludedFiles)

    if ($DryRun) {
        $args += "/L"
    }

    Write-Host "Running robocopy mirror..."
    & $robocopy @args
    $rc = $LASTEXITCODE
    if ($rc -ge 8) {
        Fail "NAS robocopy failed with exit code $rc."
    }
    Write-Host "NAS backup finished. Robocopy exit code: $rc"
}

function Remove-ExcludedGitTracking([string]$Repo) {
    $patterns = @(
        ":(glob)**/.DS_Store",
        ":(glob)**/Thumbs.db",
        ":(glob)**/Desktop.ini",
        ":(glob)**/*~",
        ":(glob)**/*.swp",
        ":(glob)**/*.swo",
        ":(glob)**/*.tmp",
        ":(glob)**/*.temp",
        ":(glob)**/*.orig",
        ":(glob)**/__pycache__/**",
        ":(glob)**/*.pyc",
        ":(glob)**/*.pyo",
        ":(glob)**/.pytest_cache/**",
        ":(glob)**/.mypy_cache/**",
        ":(glob)**/.ruff_cache/**",
        ":(glob)**/.coverage",
        ":(glob)**/htmlcov/**",
        ":(glob)**/node_modules/**",
        ":(glob)**/.npm/**",
        ":(glob)**/.pnpm-store/**",
        ":(glob)**/.vs/**",
        ":(glob)**/CMakeFiles/**",
        ":(glob)**/CMakeCache.txt",
        ":(glob)**/cmake_install.cmake",
        ":(glob)**/compile_commands.json",
        ":(glob)**/bin/**",
        ":(glob)**/obj/**",
        ":(glob)**/build/**",
        ":(glob)**/build-*/**",
        ":(glob)**/cmake-build-*/**",
        ":(glob)**/out/**",
        ":(glob)**/dist/**",
        ":(glob)**/tmp/**",
        ":(glob)**/temp/**",
        ":(glob)**/logs/**",
        ":(glob)**/.Xil/**",
        ":(glob)**/*.cache/**",
        ":(glob)**/*.gen/**",
        ":(glob)**/*.hw/**",
        ":(glob)**/*.runs/**",
        ":(glob)**/*.jou",
        ":(glob)**/*.str",
        ":(glob)**/*.log",
        ":(glob)**/*.wdb",
        ":(glob)**/*.vcd",
        ":(glob)**/*.zip",
        ":(glob)**/*.7z",
        ":(glob)**/*.rar",
        ":(glob)**/*.tar",
        ":(glob)**/*.tar.gz",
        ":(glob)**/*.tgz",
        ":(glob)**/*.tar.bz2",
        ":(glob)**/*.tbz2",
        ":(glob)**/*.tar.xz",
        ":(glob)**/*.txz",
        ":(glob)**/*.gz",
        ":(glob)**/*.bz2",
        ":(glob)**/*.xz",
        ":(glob)**/*.zst",
        ":(glob)**/*.deb",
        ":(glob)**/*.rpm",
        ":(glob)**/*.msi",
        ":(glob)**/*.dmg",
        ":(glob)**/*.pkg",
        ":(glob)**/*.AppImage",
        ":(glob)**/*.iso",
        ":(glob)**/artifacts/**",
        ":(glob)**/evidence/**",
        ":(glob)**/evidence_*/**",
        ":(glob)**/*_evidence/**",
        ":(glob)**/*evidence*/**",
        ":(glob)repo_ignored/**",
        ":(glob)**/repo_ignored/**"
    )

    foreach ($pattern in $patterns) {
        Invoke-GitChecked -Repo $Repo -GitArgs @("rm", "--cached", "-r", "--ignore-unmatch", "--", $pattern) | Out-Null
    }
}

function Invoke-GitPushRepo([string]$Repo, [string]$Remote, [string]$Branch, [switch]$DryRun) {
    Write-Host ""
    Write-Host "=== Push v5 to git ==="
    Write-Host "Repo:   $Repo"
    Write-Host "Remote: $Remote"
    Write-Host "Branch: $Branch"
    if ($DryRun) {
        Write-Host "Mode:   git dry-run, no commit or push"
    }

    Ensure-GitRepository $Repo $Remote $Branch

    Write-Host ""
    Write-Host "Checking git status..."
    Invoke-GitChecked -Repo $Repo -GitArgs @("status", "-sb") | Out-Null

    if ($DryRun) {
        Write-Host "Checking add scope without changing the index..."
        Invoke-GitChecked -Repo $Repo -GitArgs @("add", "-A", "--dry-run") | Out-Null
        Write-Host "Dry-run complete. Skipping commit and push."
        return
    }

    Write-Host ""
    Write-Host "Staging all v5 changes..."
    Invoke-GitChecked -Repo $Repo -GitArgs @("add", "-A") | Out-Null

    Write-Host "Removing cache, temporary, generated build, package, and proof files from git tracking..."
    Remove-ExcludedGitTracking $Repo

    Write-Host "Checking staged patch after exclusions..."
    $checkRc = Invoke-GitChecked -Repo $Repo -GitArgs @("diff", "--cached", "--check") -AllowFailure
    if ($checkRc -ne 0) {
        Write-Host "Whitespace warnings found; continuing because full source tracking includes imported/upstream files." -ForegroundColor Yellow
    }

    $hasNoStagedChanges = Invoke-GitChecked -Repo $Repo -GitArgs @("diff", "--cached", "--quiet") -AllowFailure
    if ($hasNoStagedChanges -ne 0) {
        $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
        $msg = "Full v5 push $stamp"
        Write-Host "Creating commit: $msg"
        Invoke-GitChecked -Repo $Repo -GitArgs @("commit", "-m", $msg) | Out-Null
    }
    else {
        Write-Host "No staged changes. Skipping commit."
    }

    Write-Host ""
    Write-Host "Pushing v5 to $Remote $Branch..."
    Invoke-GitChecked -Repo $Repo -GitArgs @("push", $Remote, $Branch) | Out-Null

    Write-Host ""
    Write-Host "Done: git push"
    Invoke-GitChecked -Repo $Repo -GitArgs @("status", "-sb") | Out-Null
}

$resolvedRepoRoot = Resolve-V5RepoRoot $RepoRoot

Write-Host "=== Push v5 to git and backup v5 to NAS ==="
Write-Host "Version: 20260707-mainline"
Write-Host "Repo:    $resolvedRepoRoot"
Write-Host "Excludes: .git, repo_ignored, cache, temporary, generated build/run outputs, packages, and proof files"
Write-Host ""
Write-Host "Scope: Windows v5 source truth only"
Write-Host "Git target: $GitRemote/$GitBranch"
Write-Host "NAS target: \\$NasHost\$NasShareName\v5"

Invoke-GitPushRepo -Repo $resolvedRepoRoot -Remote $GitRemote -Branch $GitBranch -DryRun:$GitDryRun
Invoke-NasMirrorBackup -SourceRoot $resolvedRepoRoot -HostName $NasHost -ShareName $NasShareName -TimeoutSeconds $NasProbeTimeoutSeconds -DryRun:$NasDryRun

Write-Host ""
Write-Host "All done."
exit 0
