param(
  [string]$ProjectDir,
  [string]$EvidencePath
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($EvidencePath)) {
  $EvidencePath = Join-Path $ProjectDir 'docs/pl_estop_board_validation_evidence.csv'
}

$EvidenceRootRelativePath = 'docs/evidence/pl_estop'

function Test-RelativeEvidencePath {
  param([string]$Value)
  if ([string]::IsNullOrWhiteSpace($Value)) {
    return $true
  }
  if ($Value -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
    return $false
  }
  if (($Value -split '[\\/]') -contains '..') {
    return $false
  }
  return $true
}

function Get-ProjectEvidenceFile {
  param(
    [string]$RootDir,
    [string]$RelativePath,
    [string]$RequiredRoot,
    [string]$RequiredSubject
  )

  if (-not (Test-RelativeEvidencePath -Value $RelativePath)) {
    return $null
  }
  $normalizedPath = ($RelativePath -replace '\\', '/').TrimStart('/')
  $normalizedRoot = ($RequiredRoot -replace '\\', '/').TrimEnd('/')
  if (-not ($normalizedPath -eq $normalizedRoot -or $normalizedPath.StartsWith("$normalizedRoot/", [System.StringComparison]::OrdinalIgnoreCase))) {
    return $null
  }
  if (-not $normalizedPath.EndsWith('.md', [System.StringComparison]::OrdinalIgnoreCase)) {
    return $null
  }
  $fullPath = Join-Path $RootDir ($normalizedPath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
  if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
    return $null
  }
  $item = Get-Item -LiteralPath $fullPath
  if ($item.Length -lt 80) {
    return $null
  }
  $text = Get-Content -LiteralPath $fullPath -Raw -Encoding UTF8
  if ($text -notmatch '(?im)^\s*Evidence State:\s*board_verified\s*$') {
    return $null
  }
  if (-not [string]::IsNullOrWhiteSpace($RequiredSubject) -and $text -notmatch [regex]::Escape($RequiredSubject)) {
    return $null
  }
  if ($text -match '(?im)^\s*(TODO|TBD|PLACEHOLDER|DRAFT)\b|TODO:|TBD:') {
    return $null
  }
  $requiredFields = @('Subject', 'Operator', 'Date', 'Instrument', 'Result', 'Attachments')
  $fieldValues = @{}
  foreach ($field in $requiredFields) {
    $fieldMatch = [regex]::Match($text, "(?im)^\s*$field\s*:\s*(.+?)\s*$")
    if (-not $fieldMatch.Success) {
      return $null
    }
    $value = $fieldMatch.Groups[1].Value.Trim()
    if ([string]::IsNullOrWhiteSpace($value) -or $value -match '<[^>]+>') {
      return $null
    }
    $fieldValues[$field] = $value
  }
  if ($fieldValues['Subject'] -notmatch [regex]::Escape($RequiredSubject)) {
    return $null
  }
  if ($fieldValues['Date'] -notmatch '^\d{4}-\d{2}-\d{2}$') {
    return $null
  }
  $normalizedRecordPath = ($RelativePath -replace '\\', '/').TrimStart('/')
  $attachments = @($fieldValues['Attachments'] -split '[,;]' | ForEach-Object { $_.Trim() } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
  if ($attachments.Count -eq 0) {
    return $null
  }
  foreach ($attachment in $attachments) {
    if ($attachment -match '(?<![A-Za-z])[A-Za-z]:[\\/]' -or (($attachment -split '[\\/]') -contains '..')) {
      return $null
    }
    $normalizedAttachment = ($attachment -replace '\\', '/').TrimStart('/')
    if (-not $normalizedAttachment.StartsWith("$normalizedRoot/", [System.StringComparison]::OrdinalIgnoreCase)) {
      return $null
    }
    if ($normalizedAttachment -eq $normalizedRecordPath) {
      return $null
    }
    $attachmentPath = Join-Path $RootDir ($normalizedAttachment -replace '/', [System.IO.Path]::DirectorySeparatorChar)
    if (-not (Test-Path -LiteralPath $attachmentPath -PathType Leaf)) {
      return $null
    }
  }
  return $item
}

if (-not (Test-Path -LiteralPath $EvidencePath)) {
  throw 'Missing docs/pl_estop_board_validation_evidence.csv'
}

$rows = @(Import-Csv -LiteralPath $EvidencePath)
if ($rows.Count -eq 0) {
  throw 'PL E-stop board validation CSV has no rows'
}

$requiredColumns = @(
  'test_id',
  'requirement_group',
  'trigger',
  'expected_result',
  'required_instrument',
  'evidence_state',
  'measured_value',
  'acceptance_limit',
  'evidence_path',
  'operator',
  'date',
  'next_action'
)

$actualColumns = @($rows[0].PSObject.Properties.Name)
foreach ($column in $requiredColumns) {
  if ($actualColumns -notcontains $column) {
    throw "Missing board validation column: $column"
  }
}

$requiredTests = @(
  'BV01','BV02','BV03','BV04','BV05','BV06','BV07',
  'BV08','BV09','BV10','BV11','BV12','BV13','BV14'
)

$byTest = @{}
foreach ($row in $rows) {
  if ([string]::IsNullOrWhiteSpace($row.test_id)) {
    throw 'Board validation row has empty test_id'
  }
  if ($byTest.ContainsKey($row.test_id)) {
    throw "Duplicate board validation test_id: $($row.test_id)"
  }
  $byTest[$row.test_id] = $row
}

foreach ($testId in $requiredTests) {
  if (-not $byTest.ContainsKey($testId)) {
    throw "Missing required board validation row: $testId"
  }
}

$allowedStates = @('pending', 'board_verified')
$verifiedRows = 0
$pendingRows = 0
$verifiedEvidenceFiles = 0
$violations = New-Object System.Collections.Generic.List[string]

foreach ($row in $rows) {
  if ($allowedStates -notcontains $row.evidence_state) {
    $violations.Add("$($row.test_id): invalid evidence_state '$($row.evidence_state)'")
    continue
  }

  if (-not (Test-RelativeEvidencePath -Value $row.evidence_path)) {
    $violations.Add("$($row.test_id): evidence_path must be project-relative")
  }

  foreach ($field in @('requirement_group', 'trigger', 'expected_result', 'required_instrument', 'acceptance_limit', 'next_action')) {
    if ([string]::IsNullOrWhiteSpace($row.$field)) {
      $violations.Add("$($row.test_id): missing $field")
    }
  }

  if ($row.evidence_state -eq 'pending') {
    $pendingRows += 1
    continue
  }

  $verifiedRows += 1
  foreach ($field in @('measured_value', 'evidence_path', 'operator', 'date')) {
    if ([string]::IsNullOrWhiteSpace($row.$field)) {
      $violations.Add("$($row.test_id): board_verified requires $field")
    }
  }
  if (-not [string]::IsNullOrWhiteSpace($row.evidence_path)) {
    $evidenceFile = Get-ProjectEvidenceFile -RootDir $ProjectDir -RelativePath $row.evidence_path -RequiredRoot $EvidenceRootRelativePath -RequiredSubject $row.test_id
    if ($null -eq $evidenceFile) {
      $violations.Add("$($row.test_id): board_verified evidence_path must point to a non-placeholder .md evidence record under $EvidenceRootRelativePath with Evidence State: board_verified and the test_id")
    } else {
      $verifiedEvidenceFiles += 1
    }
  }
}

if ($violations.Count -gt 0) {
  Write-Output 'board_validation_violations:'
  foreach ($violation in $violations) {
    Write-Output "  $violation"
  }
  throw "PL E-stop board validation violations found: $($violations.Count)"
}

$requiredCount = $requiredTests.Count
$boardValidationReady = ($verifiedRows -eq $requiredCount)
$state = if ($boardValidationReady) { 'board_verified_ready' } else { 'not_ready' }

Write-Output "pl_estop_board_validation=$state"
Write-Output "board_validation_ready=$(if ($boardValidationReady) { 'yes' } else { 'no' })"
Write-Output "required_tests=$requiredCount"
Write-Output "verified_tests=$verifiedRows"
Write-Output "pending_tests=$pendingRows"
Write-Output "verified_evidence_files=$verifiedEvidenceFiles"
Write-Output 'board_verified_evidence_contract=md_non_placeholder'
Write-Output 'board_verified_attachment_contract=project_relative_existing_files'
