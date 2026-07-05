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
if (-not (Test-Path -LiteralPath $activeXdcPath)) {
  throw "Missing active XDC: $activeXdcPath"
}

$activeText = Get-Content -LiteralPath $activeXdcPath -Raw -Encoding UTF8
$readme = Read-ProjectText -RootDir $ProjectDir -RelativePath 'README.md'

if ($activeText -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
  throw 'Active XDC contains a drive-letter absolute path'
}
if ($activeText -match 'vivado_hw_project|constrs_1/system\.xdc') {
  throw 'Active XDC references the old Vivado project or old system.xdc'
}

$packageRows = New-Object System.Collections.Generic.List[object]
$iostandardRows = New-Object System.Collections.Generic.List[object]
$violations = New-Object System.Collections.Generic.List[string]
$malformedIostandardLines = 0

$lines = $activeText -split '\r?\n'
for ($i = 0; $i -lt $lines.Count; $i++) {
  $line = $lines[$i]
  if ($line -match '^\s*set_property\s+PACKAGE_PIN\s+(?<pin>[A-Z0-9]+)\s+\[get_ports\s+\{(?<port>[^}]+)\}\]\s*$') {
    $packageRows.Add([pscustomobject]@{
        line = $i + 1
        port = $Matches.port
        pin = $Matches.pin
      })
    continue
  }

  if ($line -match '^\s*set_property\s+IOSTANDARD\b') {
    if ($line -match '^\s*set_property\s+IOSTANDARD\s+(?<standard>[A-Za-z0-9_]+)\s+\[get_ports\s+\{(?<port>[^}]+)\}\]\s*$') {
      $iostandardRows.Add([pscustomobject]@{
          line = $i + 1
          port = $Matches.port
          standard = $Matches.standard
        })
    } else {
      $malformedIostandardLines += 1
      $violations.Add("$($i + 1): malformed IOSTANDARD constraint: $line")
    }
  }
}

if ($packageRows.Count -eq 0) {
  throw 'Active XDC has no PACKAGE_PIN assignments'
}

$portsWithPackage = @{}
foreach ($row in $packageRows) {
  $portsWithPackage[$row.port] = $true
}

$iostandardByPort = @{}
foreach ($row in $iostandardRows) {
  if (-not $iostandardByPort.ContainsKey($row.port)) {
    $iostandardByPort[$row.port] = New-Object System.Collections.Generic.List[object]
  }
  $iostandardByPort[$row.port].Add($row)
}

$missingIostandardCount = 0
$nonLvcmos33Count = 0
foreach ($row in $packageRows) {
  $portName = [string]$row.port
  if (-not $iostandardByPort.ContainsKey($portName)) {
    $missingIostandardCount += 1
    $violations.Add("$($row.line): $($row.port)/$($row.pin) is missing IOSTANDARD")
    continue
  }

  $stdMatches = @($iostandardByPort[$portName].ToArray())
  if ($stdMatches.Count -ne 1) {
    $violations.Add("$($row.line): $($row.port)/$($row.pin) has $($stdMatches.Count) IOSTANDARD constraints")
    continue
  }

  if ($stdMatches[0].standard -ne 'LVCMOS33') {
    $nonLvcmos33Count += 1
    $violations.Add("$($stdMatches[0].line): $($row.port) uses IOSTANDARD $($stdMatches[0].standard), expected LVCMOS33")
  }
}

$orphanIostandardCount = 0
foreach ($row in $iostandardRows) {
  if (-not $portsWithPackage.ContainsKey($row.port)) {
    $orphanIostandardCount += 1
    $violations.Add("$($row.line): IOSTANDARD on $($row.port) has no matching PACKAGE_PIN")
  }
}

$duplicateIostandardPorts = @($iostandardRows | Group-Object port | Where-Object { $_.Count -gt 1 })
foreach ($dup in $duplicateIostandardPorts) {
  $violations.Add("Duplicate active XDC IOSTANDARD assignment: $($dup.Name):$($dup.Count)")
}

$lvcmos33Count = @($iostandardRows | Where-Object { $_.standard -eq 'LVCMOS33' }).Count

if ($readme -notmatch 'verify_active_xdc_electrical_contract\.ps1' -or
    $readme -notmatch 'active_xdc_electrical_contract=ok') {
  $violations.Add('README does not record the active XDC electrical contract gate')
}

if ($violations.Count -gt 0) {
  Write-Output 'active_xdc_electrical_contract_violations:'
  foreach ($violation in $violations) {
    Write-Output "  $violation"
  }
  throw "Active XDC electrical contract violations found: $($violations.Count)"
}

Write-Output 'active_xdc_electrical_contract=ok'
Write-Output "active_pin_assignments=$($packageRows.Count)"
Write-Output "iostandard_assignments=$($iostandardRows.Count)"
Write-Output "lvcmos33_assignments=$lvcmos33Count"
Write-Output "missing_iostandard_assignments=$missingIostandardCount"
Write-Output "orphan_iostandard_assignments=$orphanIostandardCount"
Write-Output "non_lvcmos33_assignments=$nonLvcmos33Count"
Write-Output "duplicate_iostandard_ports=$($duplicateIostandardPorts.Count)"
Write-Output "malformed_iostandard_lines=$malformedIostandardLines"
