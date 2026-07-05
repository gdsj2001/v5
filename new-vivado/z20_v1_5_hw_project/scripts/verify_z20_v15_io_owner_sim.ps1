param(
  [string]$ProjectDir
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

$rtl = Join-Path $ProjectDir 'rtl/z20_v15_io_owner_axi_lite.v'
$tb = Join-Path $ProjectDir 'sim/z20_v15_io_owner_axi_lite_tb.v'
foreach ($required in @($rtl, $tb)) {
  if (-not (Test-Path -LiteralPath $required)) {
    throw "Missing file: $required"
  }
}

$iverilog = Get-Command iverilog -ErrorAction SilentlyContinue
$vvp = Get-Command vvp -ErrorAction SilentlyContinue
if ($null -eq $iverilog -or $null -eq $vvp) {
  throw 'iverilog/vvp not found in PATH'
}

$scratch = Join-Path $ProjectDir 'repo_ignored/io_owner_completion/scratch/sim'
New-Item -ItemType Directory -Force -Path $scratch | Out-Null
$outFile = Join-Path $scratch 'z20_v15_io_owner_axi_lite_tb.vvp'

& $iverilog.Source -g2012 -o $outFile $rtl $tb
if ($LASTEXITCODE -ne 0) {
  throw 'iverilog compile failed for z20_v15_io_owner_axi_lite_tb'
}

$output = @(& $vvp.Source $outFile)
if ($LASTEXITCODE -ne 0) {
  throw "vvp failed for z20_v15_io_owner_axi_lite_tb: $($output -join '; ')"
}
if (-not ($output -contains 'PASS: z20_v15_io_owner_axi_lite_tb')) {
  throw "missing PASS line from z20_v15_io_owner_axi_lite_tb: $($output -join '; ')"
}

Remove-Item -LiteralPath $outFile -Force -ErrorAction SilentlyContinue

Write-Output 'z20_v15_io_owner_sim=ok'
Write-Output 'z20_v15_io_owner_axi_lite_tb=pass'
Write-Output 'sim_outputs=persistent_none'
