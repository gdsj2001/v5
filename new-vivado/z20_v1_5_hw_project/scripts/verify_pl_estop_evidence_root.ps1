param(
  [string]$ProjectDir,
  [string]$EvidenceRoot,
  [string]$WiringPath,
  [string]$BoardValidationPath
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
  $EvidenceRoot = Join-Path $ProjectDir 'docs/evidence/pl_estop'
}
if ([string]::IsNullOrWhiteSpace($WiringPath)) {
  $WiringPath = Join-Path $ProjectDir 'docs/pl_estop_wiring_evidence.csv'
}
if ([string]::IsNullOrWhiteSpace($BoardValidationPath)) {
  $BoardValidationPath = Join-Path $ProjectDir 'docs/pl_estop_board_validation_evidence.csv'
}

$EvidenceRootRelativePath = 'docs/evidence/pl_estop'

function Convert-ToProjectRelativePath {
  param(
    [string]$RootDir,
    [string]$Path
  )

  $root = (Resolve-Path -LiteralPath $RootDir).Path.TrimEnd('\', '/')
  $full = (Resolve-Path -LiteralPath $Path).Path
  if (-not $full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Path is outside project: $Path"
  }
  return ($full.Substring($root.Length).TrimStart('\', '/') -replace '\\', '/')
}

function Test-ProjectRelativeEvidencePath {
  param([string]$Value)
  if ([string]::IsNullOrWhiteSpace($Value)) {
    return $false
  }
  if ($Value -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
    return $false
  }
  if (($Value -split '[\\/]') -contains '..') {
    return $false
  }
  $normalizedPath = ($Value -replace '\\', '/').TrimStart('/')
  $normalizedRoot = ($EvidenceRootRelativePath -replace '\\', '/').TrimEnd('/')
  return ($normalizedPath.StartsWith("$normalizedRoot/", [System.StringComparison]::OrdinalIgnoreCase))
}

function Add-ReferencedEvidencePath {
  param(
    [System.Collections.Generic.HashSet[string]]$Set,
    [string]$Path
  )

  if ([string]::IsNullOrWhiteSpace($Path)) {
    return
  }
  if (-not (Test-ProjectRelativeEvidencePath -Value $Path)) {
    return
  }
  $null = $Set.Add(($Path -replace '\\', '/').TrimStart('/'))
}

if (-not (Test-Path -LiteralPath $EvidenceRoot -PathType Container)) {
  throw 'Missing docs/evidence/pl_estop evidence root'
}
if (-not (Test-Path -LiteralPath $WiringPath -PathType Leaf)) {
  throw 'Missing docs/pl_estop_wiring_evidence.csv'
}
if (-not (Test-Path -LiteralPath $BoardValidationPath -PathType Leaf)) {
  throw 'Missing docs/pl_estop_board_validation_evidence.csv'
}

$wiringRows = @(Import-Csv -LiteralPath $WiringPath)
$boardRows = @(Import-Csv -LiteralPath $BoardValidationPath)
$referencedEvidence = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

foreach ($row in $wiringRows) {
  if ($row.evidence_state -eq 'board_verified') {
    Add-ReferencedEvidencePath -Set $referencedEvidence -Path $row.bench_evidence
    Add-ReferencedEvidencePath -Set $referencedEvidence -Path $row.board_evidence
  }
}
foreach ($row in $boardRows) {
  if ($row.evidence_state -eq 'board_verified') {
    Add-ReferencedEvidencePath -Set $referencedEvidence -Path $row.evidence_path
  }
}

$files = @(Get-ChildItem -LiteralPath $EvidenceRoot -Recurse -File)
$mdFiles = @($files | Where-Object { $_.Extension -ieq '.md' })
$boardVerifiedRecords = New-Object System.Collections.Generic.List[string]
$orphanRecords = New-Object System.Collections.Generic.List[string]
$violations = New-Object System.Collections.Generic.List[string]

foreach ($file in $files) {
  $relativePath = Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $file.FullName
  if ($relativePath -match '(?i)(^|/)(process|过程)\.md$') {
    $violations.Add("${relativePath}: process log is not allowed in evidence root")
  }
}

foreach ($file in $mdFiles) {
  $relativePath = Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $file.FullName
  if ($relativePath -eq "$EvidenceRootRelativePath/README.md") {
    continue
  }
  $text = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
  if ($text -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
    $violations.Add("${relativePath}: contains an absolute path")
  }
  if ($text -match 'vivado_hw_project') {
    $violations.Add("${relativePath}: contains an old-project reference")
  }
  if ($text -match '(?im)^\s*Evidence State:\s*board_verified\s*$') {
    $boardVerifiedRecords.Add($relativePath)
    if (-not $referencedEvidence.Contains($relativePath)) {
      $orphanRecords.Add($relativePath)
      $violations.Add("${relativePath}: board_verified record is not referenced by a board_verified CSV row")
    }
  }
}

if ($violations.Count -gt 0) {
  Write-Output 'evidence_root_violations:'
  foreach ($violation in $violations) {
    Write-Output "  $violation"
  }
  throw "PL E-stop evidence root violations found: $($violations.Count)"
}

Write-Output 'pl_estop_evidence_root_verify=ok'
Write-Output "evidence_root=$EvidenceRootRelativePath"
Write-Output "evidence_root_files=$($files.Count)"
Write-Output "evidence_root_md_files=$($mdFiles.Count)"
Write-Output "referenced_board_verified_evidence_paths=$($referencedEvidence.Count)"
Write-Output "board_verified_records=$($boardVerifiedRecords.Count)"
Write-Output "orphan_board_verified_records=$($orphanRecords.Count)"
