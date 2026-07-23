[CmdletBinding()]
param(
    [string]$RepositoryRoot
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
    $root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..')).ProviderPath
}
else {
    $root = (Resolve-Path -LiteralPath $RepositoryRoot).ProviderPath
}

$gate = Join-Path $root 'board\tools\ci\run_v5_host_gate.ps1'
if (-not [IO.File]::Exists($gate)) {
    throw "Host gate not found: $gate"
}

$hostExecutable = (Get-Process -Id $PID).Path
$environmentName = 'V5_HOST_GATE_ALLOW_TEST_FAILURE_INJECTION'
$previousValue = [Environment]::GetEnvironmentVariable(
    $environmentName,
    [EnvironmentVariableTarget]::Process
)
$previousErrorActionPreference = $ErrorActionPreference

try {
    [Environment]::SetEnvironmentVariable(
        $environmentName,
        '1',
        [EnvironmentVariableTarget]::Process
    )
    $ErrorActionPreference = 'Continue'
    $output = @(
        & $hostExecutable `
            -NoProfile `
            -ExecutionPolicy Bypass `
            -File $gate `
            -RepositoryRoot $root `
            -CMakeOnly `
            -TestFailStage diff-hygiene 2>&1
    )
    $exitCode = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $previousErrorActionPreference
    [Environment]::SetEnvironmentVariable(
        $environmentName,
        $previousValue,
        [EnvironmentVariableTarget]::Process
    )
}

$outputText = $output -join [Environment]::NewLine
Write-Host $outputText

if ($exitCode -eq 0) {
    throw 'Injected host-gate failure unexpectedly returned success.'
}
if ($outputText -notmatch 'V5_HOST_GATE_INJECTED_FAILURE:diff-hygiene') {
    throw 'Injected failure marker was not propagated by the host gate.'
}
if ($outputText -match '(?m)^\[document-routes\]$') {
    throw 'Host gate continued to document-routes after the injected failure.'
}

Write-Host 'V5 Windows host gate fail-fast smoke: PASS'
