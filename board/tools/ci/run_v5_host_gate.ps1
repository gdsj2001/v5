[CmdletBinding()]
param(
    [string]$RepositoryRoot,
    [string]$DiffBase,
    [switch]$CMakeOnly,
    [ValidateSet(
        'tool-preflight',
        'diff-hygiene',
        'document-routes',
        'windows-cmake-configure',
        'windows-target-boundary',
        'windows-default-build',
        'windows-local-regression',
        'python-focused-tests',
        'winremote-security',
        'factory-client-build',
        'dealer-client-build'
    )]
    [string]$TestFailStage
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$script:StageResults = [System.Collections.Generic.List[object]]::new()
$script:DiffBaseWasProvided = $PSBoundParameters.ContainsKey('DiffBase')

function Resolve-RepositoryRoot {
    if (-not [string]::IsNullOrWhiteSpace($RepositoryRoot)) {
        return (Resolve-Path -LiteralPath $RepositoryRoot).ProviderPath
    }

    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..')).ProviderPath
}

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory
    )

    Write-Host ("  > {0} {1}" -f $FilePath, ($Arguments -join ' '))
    Push-Location -LiteralPath $WorkingDirectory
    try {
        & $FilePath @Arguments
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }

    if ($exitCode -ne 0) {
        throw "$FilePath exited with code $exitCode"
    }
}

function Invoke-Stage {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][scriptblock]$Action
    )

    Write-Host ""
    Write-Host "[$Name]"
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        if ($Name -eq $TestFailStage) {
            if ($env:V5_HOST_GATE_ALLOW_TEST_FAILURE_INJECTION -ne '1') {
                throw 'Host-gate failure injection is restricted to the focused self-test.'
            }
            throw "V5_HOST_GATE_INJECTED_FAILURE:$Name"
        }
        & $Action
        $timer.Stop()
        $script:StageResults.Add([pscustomobject]@{
            Stage = $Name
            Result = 'PASS'
            Seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 2)
        })
        Write-Host ("PASS {0} ({1:N2}s)" -f $Name, $timer.Elapsed.TotalSeconds)
    }
    catch {
        $timer.Stop()
        $script:StageResults.Add([pscustomobject]@{
            Stage = $Name
            Result = 'FAIL'
            Seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 2)
        })
        Write-Host ("FAIL {0} ({1:N2}s)" -f $Name, $timer.Elapsed.TotalSeconds)
        throw
    }
}

function Assert-CommandAvailable {
    param([Parameter(Mandatory = $true)][string]$Name)

    if ($null -eq (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required host tool is missing: $Name"
    }
}

function Assert-DotNetSdkVersion {
    param(
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string]$ExpectedVersion
    )

    Write-Host ("  > dotnet --version (cwd={0})" -f $WorkingDirectory)
    Push-Location -LiteralPath $WorkingDirectory
    try {
        $output = @(& dotnet --version)
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }

    if ($exitCode -ne 0) {
        throw "dotnet --version exited with code $exitCode"
    }
    $actualVersion = ($output -join [Environment]::NewLine).Trim()
    Write-Host $actualVersion
    if ($actualVersion -ne $ExpectedVersion) {
        throw "WinRemote SDK mismatch: expected $ExpectedVersion from global.json, got $actualVersion"
    }
}

function Test-GitCommitAvailable {
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string]$Revision
    )

    Push-Location -LiteralPath $RepositoryRoot
    try {
        & git rev-parse --verify --quiet "$Revision^{commit}" *> $null
        return ($LASTEXITCODE -eq 0)
    }
    finally {
        Pop-Location
    }
}

function Reset-WindowsPresetBuild {
    param(
        [Parameter(Mandatory = $true)][string]$Root
    )

    $ignoredRoot = [IO.Path]::GetFullPath(
        (Join-Path $Root 'repo_ignored\windows_host_gate')
    ).TrimEnd([IO.Path]::DirectorySeparatorChar)
    $buildRoot = [IO.Path]::GetFullPath(
        (Join-Path $ignoredRoot 'windows-ci')
    )
    $requiredPrefix = $ignoredRoot + [IO.Path]::DirectorySeparatorChar

    if (-not $buildRoot.StartsWith($requiredPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to reset build directory outside host-gate scratch: $buildRoot"
    }
    if (Test-Path -LiteralPath $buildRoot) {
        Remove-Item -LiteralPath $buildRoot -Recurse -Force
    }
}

$root = Resolve-RepositoryRoot
$boardRoot = Join-Path $root 'board'
$winRemoteRoot = Join-Path $root '8ax-win'
$winRemoteGlobalJson = Join-Path $winRemoteRoot 'global.json'
$requiredFiles = @(
    (Join-Path $root 'AGENTS.md'),
    (Join-Path $boardRoot 'CMakePresets.json'),
    (Join-Path $boardRoot 'app\CMakeLists.txt'),
    (Join-Path $root 'board\tools\docs\verify_v5_document_routes.py'),
    $winRemoteGlobalJson
)

if (-not [IO.Directory]::Exists((Join-Path $root '.git'))) {
    throw "Git repository not found: $root"
}
foreach ($requiredFile in $requiredFiles) {
    if (-not [IO.File]::Exists($requiredFile)) {
        throw "Required host-gate input is missing: $requiredFile"
    }
}
if ($env:OS -ne 'Windows_NT') {
    throw 'run_v5_host_gate.ps1 only supports the canonical Windows host gate.'
}
$winRemoteSdkVersion = [string](
    (Get-Content -LiteralPath $winRemoteGlobalJson -Raw | ConvertFrom-Json).sdk.version
)
if ([string]::IsNullOrWhiteSpace($winRemoteSdkVersion)) {
    throw "WinRemote SDK version is missing from $winRemoteGlobalJson"
}

$failed = $null
try {
    Invoke-Stage -Name 'tool-preflight' -Action {
        foreach ($tool in @('git', 'python', 'cmake', 'ninja', 'clang', 'clang++')) {
            Assert-CommandAvailable -Name $tool
        }
        if (-not $CMakeOnly) {
            Assert-CommandAvailable -Name 'dotnet'
        }
        Invoke-CheckedCommand -FilePath 'cmake' -Arguments @('--version') -WorkingDirectory $root
        Invoke-CheckedCommand -FilePath 'python' -Arguments @('--version') -WorkingDirectory $root
        if (-not $CMakeOnly) {
            Assert-DotNetSdkVersion `
                -WorkingDirectory $winRemoteRoot `
                -ExpectedVersion $winRemoteSdkVersion
        }
    }

    Invoke-Stage -Name 'diff-hygiene' -Action {
        Invoke-CheckedCommand -FilePath 'git' -Arguments @('diff', '--check') -WorkingDirectory $root
        Invoke-CheckedCommand -FilePath 'git' -Arguments @('diff', '--cached', '--check') -WorkingDirectory $root

        $effectiveDiffBase = $DiffBase
        $useHeadOnly = $script:DiffBaseWasProvided -and
            ([string]::IsNullOrWhiteSpace($effectiveDiffBase) -or
             $effectiveDiffBase -match '^0{40}$')
        if ($useHeadOnly) {
            $effectiveDiffBase = $null
        }
        if (-not $useHeadOnly -and
            [string]::IsNullOrWhiteSpace($effectiveDiffBase) -and
            (Test-GitCommitAvailable -RepositoryRoot $root -Revision 'origin/main')) {
            $effectiveDiffBase = 'origin/main'
        }

        if ([string]::IsNullOrWhiteSpace($effectiveDiffBase)) {
            Invoke-CheckedCommand -FilePath 'git' -Arguments @(
                'show',
                '--check',
                '--format=',
                'HEAD'
            ) -WorkingDirectory $root
        }
        else {
            if (-not (Test-GitCommitAvailable -RepositoryRoot $root -Revision $effectiveDiffBase)) {
                throw "Diff base commit is unavailable: $effectiveDiffBase"
            }
            Invoke-CheckedCommand -FilePath 'git' -Arguments @(
                'diff',
                '--check',
                $effectiveDiffBase,
                'HEAD'
            ) -WorkingDirectory $root
        }
    }

    Invoke-Stage -Name 'document-routes' -Action {
        Invoke-CheckedCommand -FilePath 'python' -Arguments @(
            'board/tools/docs/verify_v5_document_routes.py',
            '--strict-details'
        ) -WorkingDirectory $root
    }

    Invoke-Stage -Name 'windows-cmake-configure' -Action {
        Reset-WindowsPresetBuild -Root $root
        Invoke-CheckedCommand -FilePath 'cmake' -Arguments @(
            '--preset',
            'windows-ci'
        ) -WorkingDirectory $boardRoot
    }

    Invoke-Stage -Name 'windows-target-boundary' -Action {
        Push-Location -LiteralPath $boardRoot
        try {
            $targetHelp = @(& cmake --build --preset windows-ci --target help 2>&1)
            $exitCode = $LASTEXITCODE
        }
        finally {
            Pop-Location
        }
        if ($exitCode -ne 0) {
            throw "Unable to enumerate Windows CMake targets; cmake exited with code $exitCode"
        }

        $targetText = $targetHelp -join "`n"
        foreach ($forbiddenTarget in @(
            'v5_status_shm_projection',
            'v5_state_publisher',
            'v5_command_gate_server',
            'v5_lvgl_shell',
            'v5_product_runtime',
            'v5_touch_diagnostics',
            'v5_cpu_usage_snapshot_smoke'
        )) {
            if ($targetText -match "(?m)^$([regex]::Escape($forbiddenTarget))(?=[:.])") {
                throw "Linux/ARM target is incorrectly defined by the Windows preset: $forbiddenTarget"
            }
        }
    }

    Invoke-Stage -Name 'windows-default-build' -Action {
        Invoke-CheckedCommand -FilePath 'cmake' -Arguments @(
            '--build',
            '--preset',
            'windows-ci'
        ) -WorkingDirectory $boardRoot
    }

    Invoke-Stage -Name 'windows-local-regression' -Action {
        Invoke-CheckedCommand -FilePath 'cmake' -Arguments @(
            '--build',
            '--preset',
            'windows-ci',
            '--target',
            'v5_local_regression'
        ) -WorkingDirectory $boardRoot
    }

    if (-not $CMakeOnly) {
        Invoke-Stage -Name 'python-focused-tests' -Action {
            foreach ($suite in @(
                'board/tools/v5_touch_calibration',
                '8ax-factory-client-source/vps-admin-api/tests',
                '8ax-factory-client-source/vps-auth-gateway/tests'
            )) {
                Invoke-CheckedCommand -FilePath 'python' -Arguments @(
                    '-m',
                    'unittest',
                    'discover',
                    '-s',
                    $suite,
                    '-p',
                    'test_*.py'
                ) -WorkingDirectory $root
            }
        }

        Invoke-Stage -Name 'winremote-security' -Action {
            $solution = Join-Path $root '8ax-win\8ax.WinRemote.sln'
            $testProject = Join-Path $root '8ax-win\tests\8ax.WinRemote.Tests\8ax.WinRemote.Tests.csproj'
            Invoke-CheckedCommand -FilePath 'dotnet' -Arguments @(
                'restore',
                $solution,
                '--ignore-failed-sources',
                '--nologo'
            ) -WorkingDirectory $winRemoteRoot
            Invoke-CheckedCommand -FilePath 'dotnet' -Arguments @(
                'build',
                $solution,
                '--configuration',
                'Release',
                '--no-restore',
                '--nologo'
            ) -WorkingDirectory $winRemoteRoot
            Invoke-CheckedCommand -FilePath 'dotnet' -Arguments @(
                'run',
                '--project',
                $testProject,
                '--configuration',
                'Release',
                '--no-build',
                '--no-restore'
            ) -WorkingDirectory $winRemoteRoot
        }

        Invoke-Stage -Name 'factory-client-build' -Action {
            Invoke-CheckedCommand -FilePath 'dotnet' -Arguments @(
                'build',
                (Join-Path $root '8ax-factory-client-source\8ax.FactoryClient\8ax.FactoryClient.csproj'),
                '--configuration',
                'Release',
                '--nologo'
            ) -WorkingDirectory $root
        }

        Invoke-Stage -Name 'dealer-client-build' -Action {
            Invoke-CheckedCommand -FilePath 'dotnet' -Arguments @(
                'build',
                (Join-Path $root '8ax-dealer-client-source\8ax.DealerClient\8ax.DealerClient.csproj'),
                '--configuration',
                'Release',
                '--nologo'
            ) -WorkingDirectory $root
        }
    }
}
catch {
    $failed = $_
}
finally {
    Write-Host ""
    Write-Host 'V5 Windows host gate summary'
    $script:StageResults | Format-Table -AutoSize
}

if ($null -ne $failed) {
    throw $failed
}

Write-Host 'V5 Windows host gate: PASS'
