param(
  [string]$ProjectDir
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

$scriptPath = Join-Path $PSScriptRoot 'generate_driver_contract.py'
if (-not (Test-Path -LiteralPath $scriptPath)) {
  throw 'Missing scripts/generate_driver_contract.py'
}

$output = @(& python $scriptPath --project-dir $ProjectDir)
if ($LASTEXITCODE -ne 0) {
  throw "generate_driver_contract.py failed: $($output -join '; ')"
}

$output
