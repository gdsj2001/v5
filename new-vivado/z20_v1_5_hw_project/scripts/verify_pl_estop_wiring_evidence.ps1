param(
  [string]$ProjectDir,
  [string]$EvidencePath
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($EvidencePath)) {
  $EvidencePath = Join-Path $ProjectDir 'docs/pl_estop_wiring_evidence.csv'
}

$EvidenceRootRelativePath = 'docs/evidence/pl_estop'

function Test-RelativeEvidenceText {
  param([string]$Value)
  if ([string]::IsNullOrWhiteSpace($Value)) {
    return $true
  }
  return ($Value -notmatch '(?<![A-Za-z])[A-Za-z]:[\\/]')
}

function Test-ProjectEvidenceFile {
  param(
    [string]$RootDir,
    [string]$RelativePath,
    [string]$RequiredRoot,
    [string]$RequiredSubject
  )

  if ([string]::IsNullOrWhiteSpace($RelativePath)) {
    return $false
  }
  if ($RelativePath -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
    return $false
  }
  if (($RelativePath -split '[\\/]') -contains '..') {
    return $false
  }
  $normalizedPath = ($RelativePath -replace '\\', '/').TrimStart('/')
  $normalizedRoot = ($RequiredRoot -replace '\\', '/').TrimEnd('/')
  if (-not ($normalizedPath -eq $normalizedRoot -or $normalizedPath.StartsWith("$normalizedRoot/", [System.StringComparison]::OrdinalIgnoreCase))) {
    return $false
  }
  if (-not $normalizedPath.EndsWith('.md', [System.StringComparison]::OrdinalIgnoreCase)) {
    return $false
  }
  $fullPath = Join-Path $RootDir ($normalizedPath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
  if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
    return $false
  }
  $item = Get-Item -LiteralPath $fullPath
  if ($item.Length -lt 80) {
    return $false
  }
  $text = Get-Content -LiteralPath $fullPath -Raw -Encoding UTF8
  if ($text -notmatch '(?im)^\s*Evidence State:\s*board_verified\s*$') {
    return $false
  }
  if (-not [string]::IsNullOrWhiteSpace($RequiredSubject) -and $text -notmatch [regex]::Escape($RequiredSubject)) {
    return $false
  }
  if ($text -match '(?im)^\s*(TODO|TBD|PLACEHOLDER|DRAFT)\b|TODO:|TBD:') {
    return $false
  }
  $requiredFields = @('Subject', 'Operator', 'Date', 'Instrument', 'Result', 'Attachments')
  $fieldValues = @{}
  foreach ($field in $requiredFields) {
    $fieldMatch = [regex]::Match($text, "(?im)^\s*$field\s*:\s*(.+?)\s*$")
    if (-not $fieldMatch.Success) {
      return $false
    }
    $value = $fieldMatch.Groups[1].Value.Trim()
    if ([string]::IsNullOrWhiteSpace($value) -or $value -match '<[^>]+>') {
      return $false
    }
    $fieldValues[$field] = $value
  }
  if ($fieldValues['Subject'] -notmatch [regex]::Escape($RequiredSubject)) {
    return $false
  }
  if ($fieldValues['Date'] -notmatch '^\d{4}-\d{2}-\d{2}$') {
    return $false
  }
  $normalizedRecordPath = ($RelativePath -replace '\\', '/').TrimStart('/')
  $attachments = @($fieldValues['Attachments'] -split '[,;]' | ForEach-Object { $_.Trim() } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
  if ($attachments.Count -eq 0) {
    return $false
  }
  foreach ($attachment in $attachments) {
    if ($attachment -match '(?<![A-Za-z])[A-Za-z]:[\\/]' -or (($attachment -split '[\\/]') -contains '..')) {
      return $false
    }
    $normalizedAttachment = ($attachment -replace '\\', '/').TrimStart('/')
    if (-not $normalizedAttachment.StartsWith("$normalizedRoot/", [System.StringComparison]::OrdinalIgnoreCase)) {
      return $false
    }
    if ($normalizedAttachment -eq $normalizedRecordPath) {
      return $false
    }
    $attachmentPath = Join-Path $RootDir ($normalizedAttachment -replace '/', [System.IO.Path]::DirectorySeparatorChar)
    if (-not (Test-Path -LiteralPath $attachmentPath -PathType Leaf)) {
      return $false
    }
  }
  return $true
}

if (-not (Test-Path -LiteralPath $EvidencePath)) {
  throw 'Missing docs/pl_estop_wiring_evidence.csv'
}

$rows = @(Import-Csv -LiteralPath $EvidencePath)
if ($rows.Count -eq 0) {
  throw 'PL E-stop wiring evidence CSV has no rows'
}

$requiredColumns = @(
  'signal_group',
  'signal_name',
  'candidate_net',
  'package_pin',
  'connector',
  'required_before',
  'evidence_state',
  'polarity_or_safe_level',
  'normal_owner',
  'pl_gate_point',
  'schematic_evidence',
  'wiring_evidence',
  'bench_evidence',
  'board_evidence',
  'next_action'
)

$actualColumns = @($rows[0].PSObject.Properties.Name)
foreach ($column in $requiredColumns) {
  if ($actualColumns -notcontains $column) {
    throw "Missing wiring evidence column: $column"
  }
}

$requiredSignals = @(
  'physical_nc_input',
  'sto_or_drive_enable',
  'z_or_vertical_axis_brake',
  'step_enable_gate',
  'DO1','DO2','DO3','DO4','DO5','DO6','DO7','DO8','DO9','DO10','DO11','DO12','DO13','DO14',
  'PWM1','PWM2',
  'bus_tx_driver_enable',
  'bus_tx_queue_flush'
)

$bySignal = @{}
foreach ($row in $rows) {
  if ([string]::IsNullOrWhiteSpace($row.signal_name)) {
    throw 'Wiring evidence row has empty signal_name'
  }
  if ($bySignal.ContainsKey($row.signal_name)) {
    throw "Duplicate wiring evidence signal_name: $($row.signal_name)"
  }
  $bySignal[$row.signal_name] = $row
}

foreach ($signal in $requiredSignals) {
  if (-not $bySignal.ContainsKey($signal)) {
    throw "Missing required wiring evidence row: $signal"
  }
}

$allowedStates = @('pending', 'ready_for_rtl_xdc', 'board_verified')
$readyRows = 0
$boardVerifiedRows = 0
$pendingRows = 0
$verifiedEvidenceFiles = 0
$violations = New-Object System.Collections.Generic.List[string]

foreach ($row in $rows) {
  if ($allowedStates -notcontains $row.evidence_state) {
    $violations.Add("$($row.signal_name): invalid evidence_state '$($row.evidence_state)'")
    continue
  }

  foreach ($field in @('schematic_evidence', 'wiring_evidence', 'bench_evidence', 'board_evidence')) {
    if (-not (Test-RelativeEvidenceText -Value $row.$field)) {
      $violations.Add("$($row.signal_name): $field contains an absolute path")
    }
  }

  if ($row.evidence_state -eq 'pending') {
    $pendingRows += 1
    continue
  }

  $readyRows += 1
  foreach ($field in @('polarity_or_safe_level', 'normal_owner', 'pl_gate_point', 'schematic_evidence', 'wiring_evidence')) {
    if ([string]::IsNullOrWhiteSpace($row.$field)) {
      $violations.Add("$($row.signal_name): ready_for_rtl_xdc requires $field")
    }
  }

  if ($row.signal_group -eq 'general_output' -and $row.pl_gate_point -and $row.pl_gate_point -notmatch 'pl_estop_general_output_gate|general_output') {
    $violations.Add("$($row.signal_name): general output gate point must use PL E-stop general output gate")
  }
  if ($row.signal_group -eq 'bus_tx_gate' -and $row.pl_gate_point -and $row.pl_gate_point -match 'PHY|RESET|LINK_POWER|LINK_CLK|RX_DISABLE') {
    $violations.Add("$($row.signal_name): bus TX gate point must not break link/reset/clock/RX")
  }

  if ($row.evidence_state -eq 'board_verified') {
    $boardVerifiedRows += 1
    foreach ($field in @('bench_evidence', 'board_evidence')) {
      if ([string]::IsNullOrWhiteSpace($row.$field)) {
        $violations.Add("$($row.signal_name): board_verified requires $field")
      } elseif (-not (Test-ProjectEvidenceFile -RootDir $ProjectDir -RelativePath $row.$field -RequiredRoot $EvidenceRootRelativePath -RequiredSubject $row.signal_name)) {
        $violations.Add("$($row.signal_name): $field must point to a non-placeholder .md evidence record under $EvidenceRootRelativePath with Evidence State: board_verified and the signal name")
      } else {
        $verifiedEvidenceFiles += 1
      }
    }
  }
}

if ($violations.Count -gt 0) {
  Write-Output 'wiring_evidence_violations:'
  foreach ($violation in $violations) {
    Write-Output "  $violation"
  }
  throw "PL E-stop wiring evidence violations found: $($violations.Count)"
}

$requiredCount = $requiredSignals.Count
$readyForRealPins = ($readyRows -eq $requiredCount)
$boardEvidenceReady = ($boardVerifiedRows -eq $requiredCount)

$state = 'not_ready'
if ($boardEvidenceReady) {
  $state = 'board_verified_ready'
} elseif ($readyForRealPins) {
  $state = 'ready_for_rtl_xdc'
}

$doPwmConfirmed = @($rows | Where-Object {
    $_.signal_group -eq 'general_output' -and $_.evidence_state -ne 'pending'
  }).Count
$busTxConfirmed = @($rows | Where-Object {
    $_.signal_group -eq 'bus_tx_gate' -and $_.evidence_state -ne 'pending'
  }).Count

Write-Output "pl_estop_wiring_evidence=$state"
Write-Output "ready_for_real_pins=$(if ($readyForRealPins) { 'yes' } else { 'no' })"
Write-Output "board_evidence_ready=$(if ($boardEvidenceReady) { 'yes' } else { 'no' })"
Write-Output "required_rows=$requiredCount"
Write-Output "ready_rows=$readyRows"
Write-Output "pending_rows=$pendingRows"
Write-Output "do_pwm_ready_rows=$doPwmConfirmed"
Write-Output "bus_tx_ready_rows=$busTxConfirmed"
Write-Output "verified_wiring_evidence_files=$verifiedEvidenceFiles"
Write-Output 'board_verified_evidence_contract=md_non_placeholder'
Write-Output 'board_verified_attachment_contract=project_relative_existing_files'
