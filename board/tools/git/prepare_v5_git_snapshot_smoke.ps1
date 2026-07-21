Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

function Invoke-TestGit {
    param(
        [Parameter(Mandatory = $true)][string]$Repository,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = @(& git -C $Repository @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorAction
    }
    if ($exitCode -ne 0) {
        throw "git $($Arguments -join ' ') failed:`n$($output -join [Environment]::NewLine)"
    }
    return @($output | ForEach-Object { $_.ToString() })
}

function Convert-TestExtendedPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return '\\?\' + [IO.Path]::GetFullPath($Path)
}

$helper = Join-Path $PSScriptRoot 'prepare_v5_git_snapshot.ps1'
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('v5-git-preflight-' + [Guid]::NewGuid().ToString('N'))
$tempExtended = Convert-TestExtendedPath -Path $tempRoot

try {
    [IO.Directory]::CreateDirectory($tempRoot) | Out-Null
    Invoke-TestGit -Repository $tempRoot -Arguments @('init', '-q') | Out-Null

    [IO.File]::WriteAllText((Join-Path $tempRoot '.gitignore'), "repo_ignored/`n")
    Invoke-TestGit -Repository $tempRoot -Arguments @('add', '--', '.gitignore') | Out-Null
    Invoke-TestGit -Repository $tempRoot -Arguments @(
        '-c', 'user.name=V5 Test',
        '-c', 'user.email=v5-test@example.invalid',
        'commit', '-q', '-m', 'baseline'
    ) | Out-Null
    [IO.File]::WriteAllText((Join-Path $tempRoot 'AGENTS.md'), "test root owner`n")

    $gitExecutable = (Get-Command git -ErrorAction Stop).Source
    $gitRoot = Split-Path -Parent (Split-Path -Parent $gitExecutable)
    $gitBash = Join-Path $gitRoot 'bin\bash.exe'
    if (-not [IO.File]::Exists($gitBash)) {
        throw "Git Bash was not found beside git.exe: $gitBash"
    }
    $env:V5_PREFLIGHT_TEST_ROOT = $tempRoot
    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $bashOutput = @(& $gitBash --noprofile --norc -c 'root=$(cygpath -u "$V5_PREFLIGHT_TEST_ROOT") && printf %s ssh-keyscan-output > "$root/NUL"' 2>&1)
        $bashExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorAction
    }
    if ($bashExitCode -ne 0) {
        throw "Git Bash failed to create the NUL fixture:`n$($bashOutput -join [Environment]::NewLine)"
    }
    [IO.File]::WriteAllText((Join-Path $tempRoot 'users'), 'rg-output')
    [IO.File]::WriteAllText((Join-Path $tempRoot 'allowed.txt'), 'registered-root-owner')
    [IO.Directory]::CreateDirectory((Join-Path $tempRoot 'src')) | Out-Null
    [IO.File]::WriteAllText((Join-Path $tempRoot 'src\source.c'), 'legitimate nested source')

    & $helper -Repository $tempRoot -BaselineRef HEAD -AllowedNewRoot @('allowed.txt')

    if ([IO.File]::Exists((Convert-TestExtendedPath -Path (Join-Path $tempRoot 'NUL')))) {
        throw 'NUL was not quarantined.'
    }
    if ([IO.File]::Exists((Join-Path $tempRoot 'users'))) {
        throw 'The unregistered root output was not quarantined.'
    }
    if (-not [IO.File]::Exists((Join-Path $tempRoot 'allowed.txt'))) {
        throw 'The explicitly allowed root owner was moved.'
    }
    if (-not [IO.File]::Exists((Join-Path $tempRoot 'AGENTS.md'))) {
        throw 'The canonical AGENTS.md root owner was moved.'
    }
    if (-not [IO.File]::Exists((Join-Path $tempRoot 'src\source.c'))) {
        throw 'A legitimate nested source file was moved.'
    }

    $quarantineBase = Join-Path $tempRoot 'repo_ignored\git_push_quarantine'
    $runs = @(Get-ChildItem -LiteralPath $quarantineBase -Directory)
    if ($runs.Count -ne 1) {
        throw "Expected one quarantine run, found $($runs.Count)."
    }
    $run = $runs[0].FullName
    if ([IO.File]::ReadAllText((Join-Path $run 'root_NUL')) -ne 'ssh-keyscan-output') {
        throw 'NUL content was not preserved in quarantine.'
    }
    if ([IO.File]::ReadAllText((Join-Path $run 'root_users')) -ne 'rg-output') {
        throw 'Root output content was not preserved in quarantine.'
    }
    $manifest = [IO.File]::ReadAllText((Join-Path $run 'manifest.tsv'))
    if ($manifest -notmatch '(?m)^NUL\t' -or $manifest -notmatch '(?m)^users\t') {
        throw 'Quarantine manifest is missing an original root path.'
    }

    & $helper -Repository $tempRoot -BaselineRef HEAD -AllowedNewRoot @('allowed.txt')
    $runsAfterSecondPass = @(Get-ChildItem -LiteralPath $quarantineBase -Directory)
    if ($runsAfterSecondPass.Count -ne 1) {
        throw 'Idempotent preflight created an unnecessary quarantine run.'
    }

    Invoke-TestGit -Repository $tempRoot -Arguments @('add', '-A', '--', '.') | Out-Null
    $staged = @(Invoke-TestGit -Repository $tempRoot -Arguments @('diff', '--cached', '--name-only', '--'))
    if ($staged -notcontains 'AGENTS.md' -or
        $staged -notcontains 'allowed.txt' -or
        $staged -notcontains 'src/source.c') {
        throw 'Expected canonical test files were not staged.'
    }
    if ($staged | Where-Object { $_ -like 'repo_ignored/*' }) {
        throw 'Quarantine content entered the Git index.'
    }

    Write-Host 'V5_GIT_ROOT_PREFLIGHT_SMOKE_OK'
}
finally {
    Remove-Item Env:V5_PREFLIGHT_TEST_ROOT -ErrorAction SilentlyContinue
    if ([IO.Directory]::Exists($tempExtended)) {
        foreach ($file in [IO.Directory]::EnumerateFiles($tempExtended, '*', [IO.SearchOption]::AllDirectories)) {
            [IO.File]::SetAttributes($file, [IO.FileAttributes]::Normal)
        }
        [IO.Directory]::Delete($tempExtended, $true)
    }
}
