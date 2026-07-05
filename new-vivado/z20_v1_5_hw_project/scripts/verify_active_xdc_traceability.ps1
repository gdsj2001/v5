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

function Read-ProjectText {
  param(
    [string]$RootDir,
    [string]$RelativePath
  )
  $path = Get-ProjectPath -RootDir $RootDir -RelativePath $RelativePath
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing required file: $RelativePath"
  }
  return Get-Content -LiteralPath $path -Raw -Encoding UTF8
}

$activeXdcPath = Get-ProjectPath -RootDir $ProjectDir -RelativePath 'constraints/z20_v1_5_active_mapped.xdc'
$sourceXdcPath = Get-ProjectPath -RootDir $ProjectDir -RelativePath '../z20-v1_5_20260623.xdc'
foreach ($path in @($activeXdcPath, $sourceXdcPath)) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing XDC traceability input: $path"
  }
}

$activeText = Get-Content -LiteralPath $activeXdcPath -Raw -Encoding UTF8
$sourceText = Get-Content -LiteralPath $sourceXdcPath -Raw -Encoding UTF8
$readme = Read-ProjectText -RootDir $ProjectDir -RelativePath 'README.md'

if ($activeText -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
  throw 'Active XDC contains a drive-letter absolute path'
}
if ($activeText -match 'vivado_hw_project|constrs_1/system\.xdc') {
  throw 'Active XDC references the old Vivado project or old system.xdc'
}
if ($activeText -notmatch 'Physical pin assignments are derived from \.\./z20-v1_5_20260623\.xdc') {
  throw 'Active XDC header does not declare the v1.5 source XDC'
}

$sourcePinsByNet = @{}
foreach ($match in [regex]::Matches($sourceText, '(?m)^\s*set_io\s+\{(?<net>[^}]+)\}\s+(?<pin>[A-Z0-9]+)\s*$')) {
  $net = $match.Groups['net'].Value
  $pin = $match.Groups['pin'].Value
  $sourcePinsByNet[$net] = $pin
}
if ($sourcePinsByNet.Count -eq 0) {
  throw 'No set_io rows parsed from v1.5 source XDC'
}

$lines = $activeText -split '\r?\n'
$assignments = New-Object System.Collections.Generic.List[object]
$violations = New-Object System.Collections.Generic.List[string]

for ($i = 0; $i -lt $lines.Count; $i++) {
  $line = $lines[$i]
  if ($line -notmatch '^\s*set_property\s+PACKAGE_PIN\s+(?<pin>[A-Z0-9]+)\s+\[get_ports\s+\{(?<port>[^}]+)\}\]\s*$') {
    continue
  }

  $pin = $Matches.pin
  $port = $Matches.port
  $sourceComment = $null
  for ($j = $i - 1; $j -ge 0; $j--) {
    $candidate = $lines[$j].Trim()
    if ([string]::IsNullOrWhiteSpace($candidate)) {
      continue
    }
    if ($candidate.StartsWith('# Source:')) {
      $sourceComment = $candidate
    }
    break
  }

  if ([string]::IsNullOrWhiteSpace($sourceComment)) {
    $violations.Add("$($i + 1): $port/$pin is missing a preceding Source comment")
    continue
  }
  if ($sourceComment -notmatch '^\# Source:\s+\.\./z20-v1_5_20260623\.xdc,\s+(?<net>[A-Za-z0-9_]+)\s+package\s+(?<sourcePin>[A-Z0-9]+)\s+->\s+current wrapper\s+(?<sourcePort>.+)$') {
    $violations.Add("$($i + 1): $port/$pin has malformed Source comment: $sourceComment")
    continue
  }

  $sourceNet = $Matches.net
  $sourcePin = $Matches.sourcePin
  $sourcePort = $Matches.sourcePort
  if ($sourcePin -ne $pin) {
    $violations.Add("$($i + 1): $port pin $pin does not match Source comment pin $sourcePin")
  }
  if ($sourcePort -ne $port) {
    $violations.Add("$($i + 1): Source comment wrapper port '$sourcePort' does not match constrained port '$port'")
  }
  if (-not $sourcePinsByNet.ContainsKey($sourceNet)) {
    $violations.Add("$($i + 1): Source net $sourceNet is not present in v1.5 source XDC")
  } elseif ($sourcePinsByNet[$sourceNet] -ne $pin) {
    $violations.Add("$($i + 1): Source net $sourceNet maps to $($sourcePinsByNet[$sourceNet]) in source XDC, not active pin $pin")
  }

  $assignments.Add([pscustomobject]@{
      port = $port
      pin = $pin
      source_net = $sourceNet
    })
}

if ($assignments.Count -eq 0) {
  throw 'Active XDC has no PACKAGE_PIN assignments'
}

$duplicatePorts = @($assignments | Group-Object port | Where-Object { $_.Count -gt 1 })
$duplicatePins = @($assignments | Group-Object pin | Where-Object { $_.Count -gt 1 })
foreach ($dup in $duplicatePorts) {
  $violations.Add("Duplicate active XDC port assignment: $($dup.Name):$($dup.Count)")
}
foreach ($dup in $duplicatePins) {
  $violations.Add("Duplicate active XDC pin assignment: $($dup.Name):$($dup.Count)")
}

$timingExceptionLines = @($lines | Where-Object { $_ -match '^\s*(create_clock|create_generated_clock|set_input_delay|set_output_delay|set_false_path|set_max_delay|set_min_delay)\b' })
foreach ($timingLine in $timingExceptionLines) {
  if ($timingLine -match '(?<![A-Za-z])[A-Za-z]:[\\/]' -or $timingLine -match 'vivado_hw_project') {
    $violations.Add("Timing line is not project-local: $timingLine")
  }
}

if ($readme -notmatch 'active constraint file is `constraints/z20_v1_5_active_mapped\.xdc`' -or
    $readme -notmatch '\.\./z20-v1_5_20260623\.xdc') {
  $violations.Add('README does not record active XDC traceability to ../z20-v1_5_20260623.xdc')
}

if ($violations.Count -gt 0) {
  Write-Output 'active_xdc_traceability_violations:'
  foreach ($violation in $violations) {
    Write-Output "  $violation"
  }
  throw "Active XDC traceability violations found: $($violations.Count)"
}

Write-Output 'active_xdc_traceability=ok'
Write-Output "active_pin_assignments=$($assignments.Count)"
Write-Output "traced_assignments=$($assignments.Count)"
Write-Output "source_xdc_nets=$($sourcePinsByNet.Count)"
Write-Output "timing_exception_lines=$($timingExceptionLines.Count)"
Write-Output 'active_xdc_source=../z20-v1_5_20260623.xdc'
Write-Output 'old_project_xdc_dependency=none'
