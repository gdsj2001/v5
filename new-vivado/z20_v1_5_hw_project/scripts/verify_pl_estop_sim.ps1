param(
  [string]$ProjectDir
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

function Get-ProjectPath {
  param(
    [string]$RootDir,
    [string]$RelativePath
  )
  return Join-Path $RootDir ($RelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
}

function Invoke-PlEstopTestbench {
  param(
    [string]$RootDir,
    [string]$TopModule,
    [string]$ExpectedPassLine
  )

  $iverilog = Get-Command iverilog -ErrorAction SilentlyContinue
  $vvp = Get-Command vvp -ErrorAction SilentlyContinue
  if ($null -eq $iverilog -or $null -eq $vvp) {
    throw 'Icarus Verilog tools are required: missing iverilog or vvp'
  }

  $corePath = Get-ProjectPath -RootDir $RootDir -RelativePath 'rtl/pl_estop_core.v'
  $axiPath = Get-ProjectPath -RootDir $RootDir -RelativePath 'rtl/pl_estop_axi_lite.v'
  $tbPath = Get-ProjectPath -RootDir $RootDir -RelativePath "sim/$TopModule.v"
  foreach ($path in @($corePath, $axiPath, $tbPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
      throw "Missing simulation input: $path"
    }
  }

  $tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("z20_pl_estop_sim_" + [System.Guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
  $compiledPath = Join-Path $tempDir "$TopModule.ivl"

  try {
    $compileOutput = @(& $iverilog.Source -g2012 -s $TopModule -o $compiledPath $corePath $axiPath $tbPath 2>&1)
    if ($LASTEXITCODE -ne 0) {
      throw "iverilog failed for ${TopModule}: $($compileOutput -join '; ')"
    }

    $runOutput = @(& $vvp.Source $compiledPath 2>&1)
    if ($LASTEXITCODE -ne 0) {
      throw "vvp failed for ${TopModule}: $($runOutput -join '; ')"
    }
    if (($runOutput -join "`n") -notmatch [regex]::Escape($ExpectedPassLine)) {
      throw "Missing expected pass line for ${TopModule}: $ExpectedPassLine"
    }
  } finally {
    if (Test-Path -LiteralPath $tempDir) {
      Remove-Item -LiteralPath $tempDir -Recurse -Force
    }
  }
}

Invoke-PlEstopTestbench -RootDir $ProjectDir -TopModule 'pl_estop_core_tb' -ExpectedPassLine 'PASS: pl_estop_core_tb'
Invoke-PlEstopTestbench -RootDir $ProjectDir -TopModule 'pl_estop_axi_lite_tb' -ExpectedPassLine 'PASS: pl_estop_axi_lite_tb'

Write-Output 'pl_estop_sim=ok'
Write-Output 'pl_estop_core_tb=pass'
Write-Output 'pl_estop_axi_lite_tb=pass'
Write-Output 'sim_tool=icarus_verilog'
Write-Output 'sim_outputs=persistent_none'
