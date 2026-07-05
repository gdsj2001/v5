param(
  [string]$ProjectDir
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

$scriptPath = Join-Path $PSScriptRoot 'generate_hardware_abi_header.py'
if (-not (Test-Path -LiteralPath $scriptPath)) {
  throw 'Missing scripts/generate_hardware_abi_header.py'
}

$output = @(& python $scriptPath --project-dir $ProjectDir)
if ($LASTEXITCODE -ne 0) {
  throw "generate_hardware_abi_header.py failed: $($output -join '; ')"
}

$output
