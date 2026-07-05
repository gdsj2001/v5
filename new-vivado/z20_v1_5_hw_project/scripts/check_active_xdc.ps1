param(
  [string]$ProjectDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

$ErrorActionPreference = 'Stop'

$xdcPath = Join-Path $ProjectDir 'constraints/z20_v1_5_active_mapped.xdc'
$projectPath = Join-Path $ProjectDir 'z20_v1_5_hw_project.xpr'

if (-not (Test-Path -LiteralPath $xdcPath)) {
  throw "Missing active XDC: $xdcPath"
}
if (-not (Test-Path -LiteralPath $projectPath)) {
  throw "Missing project file: $projectPath"
}

function Get-CurrentTopSource {
  param([string]$RootDir)

  $xprPath = Join-Path $RootDir 'z20_v1_5_hw_project.xpr'
  $xprText = Get-Content -LiteralPath $xprPath -Raw
  $topModule = 'system_wrapper'
  if ($xprText -match '<Option\s+Name="TopModule"\s+Val="([^"]+)"') {
    $topModule = $Matches[1]
  }

  $candidates = @(
    (Join-Path $RootDir "rtl/$topModule.v"),
    (Join-Path $RootDir "z20_v1_5_hw_project.srcs/sources_1/imports/hdl/$topModule.v")
  )
  foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath $candidate) {
      return [pscustomobject]@{
        Module = $topModule
        Path = (Resolve-Path -LiteralPath $candidate).Path
      }
    }
  }

  $modulePattern = "(?m)^\s*module\s+$([regex]::Escape($topModule))\b"
  $source = Get-ChildItem -LiteralPath $RootDir -File -Recurse -Include '*.v','*.vh' |
    Where-Object { $_.FullName -notmatch '(\\|/)([^\\/]*\.runs|[^\\/]*\.gen|[^\\/]*\.cache|\.Xil)(\\|/)' } |
    Where-Object { (Get-Content -LiteralPath $_.FullName -Raw) -match $modulePattern } |
    Select-Object -First 1

  if (-not $source) {
    throw "Could not find source file for top module: $topModule"
  }
  return [pscustomobject]@{
    Module = $topModule
    Path = $source.FullName
  }
}

function Expand-PortName {
  param(
    [string]$Name,
    [string]$Range
  )
  if ([string]::IsNullOrWhiteSpace($Range)) {
    return @($Name)
  }
  if ($Range -notmatch '\[\s*(\d+)\s*:\s*(\d+)\s*\]') {
    return @($Name)
  }
  $left = [int]$Matches[1]
  $right = [int]$Matches[2]
  $min = [Math]::Min($left, $right)
  $max = [Math]::Max($left, $right)
  $items = @()
  for ($i = $min; $i -le $max; $i++) {
    $items += "$Name[$i]"
  }
  return $items
}

$topInfo = Get-CurrentTopSource -RootDir $ProjectDir
$topText = Get-Content -LiteralPath $topInfo.Path -Raw
$declaredBase = @{}
$declaredExpanded = New-Object System.Collections.Generic.List[string]

$declPattern = '(?m)^\s*(input|output|inout)\s+(?:wire\s+|reg\s+)?(?<range>\[[^\]]+\]\s*)?(?<name>[A-Za-z_][A-Za-z0-9_]*)\s*;'
foreach ($match in [regex]::Matches($topText, $declPattern)) {
  $name = $match.Groups['name'].Value
  $range = $match.Groups['range'].Value
  $declaredBase[$name] = $true
  foreach ($expanded in Expand-PortName -Name $name -Range $range) {
    $declaredExpanded.Add($expanded)
  }
}

$activePorts = New-Object System.Collections.Generic.List[string]
$activePins = New-Object System.Collections.Generic.List[string]

foreach ($line in Get-Content -LiteralPath $xdcPath) {
  if ($line -match 'PACKAGE_PIN\s+([^\s]+).*\[get_ports\s+\{([^}]+)\}\]') {
    $activePins.Add($Matches[1])
    $activePorts.Add($Matches[2])
  }
}

$missing = @()
foreach ($port in $activePorts) {
  $base = $port -replace '\[[0-9]+\]$', ''
  if (-not $declaredBase.ContainsKey($base)) {
    $missing += $port
  }
}

$duplicatePorts = $activePorts | Group-Object | Where-Object Count -gt 1 | ForEach-Object { "$($_.Name):$($_.Count)" }
$duplicatePins = $activePins | Group-Object | Where-Object Count -gt 1 | ForEach-Object { "$($_.Name):$($_.Count)" }

$activeSet = @{}
foreach ($port in $activePorts) {
  $activeSet[$port] = $true
}

$ignorePatterns = @(
  '^DDR_',
  '^FIXED_IO_'
)

$unassigned = @()
foreach ($port in $declaredExpanded) {
  if ($activeSet.ContainsKey($port)) {
    continue
  }
  $ignore = $false
  foreach ($pattern in $ignorePatterns) {
    if ($port -match $pattern) {
      $ignore = $true
      break
    }
  }
  if (-not $ignore) {
    $unassigned += $port
  }
}

$topDisplay = $topInfo.Path
$projectFullPath = (Resolve-Path -LiteralPath $ProjectDir).Path
if ($topDisplay.StartsWith($projectFullPath, [System.StringComparison]::OrdinalIgnoreCase)) {
  $topDisplay = $topDisplay.Substring($projectFullPath.Length) -replace '^[\\/]+', ''
}
Write-Output "top_module=$($topInfo.Module) top_source=$topDisplay"
Write-Output "active_assignments=$($activePorts.Count) missing=[$($missing -join ',')] duplicate_ports={$(($duplicatePorts -join ', '))} duplicate_pins={$(($duplicatePins -join ', '))}"
Write-Output "unassigned_top_ports_count=$($unassigned.Count)"
if ($unassigned.Count -gt 0) {
  Write-Output 'unassigned_top_ports='
  foreach ($port in ($unassigned | Sort-Object)) {
    Write-Output "  $port"
  }
}

if ($missing.Count -gt 0 -or $duplicatePorts.Count -gt 0 -or $duplicatePins.Count -gt 0) {
  exit 1
}
