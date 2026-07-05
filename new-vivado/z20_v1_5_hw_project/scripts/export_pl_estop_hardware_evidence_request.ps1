param(
  [string]$ProjectDir,
  [string]$WiringPath,
  [string]$BoardValidationPath,
  [string]$OutPath
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($WiringPath)) {
  $WiringPath = Join-Path $ProjectDir 'docs/pl_estop_wiring_evidence.csv'
}
if ([string]::IsNullOrWhiteSpace($BoardValidationPath)) {
  $BoardValidationPath = Join-Path $ProjectDir 'docs/pl_estop_board_validation_evidence.csv'
}
if ([string]::IsNullOrWhiteSpace($OutPath)) {
  $OutPath = Join-Path $ProjectDir 'docs/pl_estop_hardware_evidence_request.md'
}

. (Join-Path $PSScriptRoot 'write_text_with_retry.ps1')

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

function Get-ProjectRelativeOutputPath {
  param(
    [string]$RootDir,
    [string]$Path
  )

  if (Test-Path -LiteralPath $Path) {
    return Convert-ToProjectRelativePath -RootDir $RootDir -Path $Path
  }
  $root = (Resolve-Path -LiteralPath $RootDir).Path.TrimEnd('\', '/')
  $parent = Split-Path -Parent $Path
  $fullParent = (Resolve-Path -LiteralPath $parent).Path
  $candidate = Join-Path $fullParent (Split-Path -Leaf $Path)
  return (($candidate.Substring($root.Length).TrimStart('\', '/')) -replace '\\', '/')
}

function Escape-MarkdownCell {
  param([string]$Value)
  if ([string]::IsNullOrWhiteSpace($Value)) {
    return ''
  }
  return (($Value -replace '\|', '/') -replace '\r?\n', ' ')
}

function Get-WiringCollectionRequest {
  param($Row)

  switch ($Row.signal_group) {
    'estop_input' {
      return 'Confirm this is the actual NC safety-chain input, including healthy level, trip level, pull state, disconnect behavior, and board node trace.'
    }
    'sto_enable_output' {
      return 'Identify the real STO or drive-enable output, confirm fail-safe polarity, and record where the PL gate must sit after the normal owner.'
    }
    'brake_output' {
      return 'Identify the vertical-axis brake output, active polarity, required lead time, and the gate point relative to the drive block.'
    }
    'axis_gate' {
      return 'Identify the real axis-output owner and final PL gate point for step, PWM, ENA, or future motion-output paths.'
    }
    'general_output' {
      return 'Confirm the connected load, off or safe polarity, normal output owner, and the post-owner PL gate point for this DO/PWM channel.'
    }
    'bus_tx_gate' {
      return 'Confirm the bus owner and a TX send-enable, driver-enable, TX idle, or TX_CTL gate point; do not target PHY reset, link clock, link power, or RX.'
    }
    default {
      return 'Confirm real wiring, safe polarity, normal owner, PL gate point, and evidence path before promotion.'
    }
  }
}

function Get-BoardCollectionRequest {
  param($Row)

  return "Measure and record $($Row.measured_value -replace '^\s*$', 'the required value') with $($Row.required_instrument); acceptance: $($Row.acceptance_limit)."
}

foreach ($path in @($WiringPath, $BoardValidationPath)) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing evidence input: $path"
  }
}

$outDir = Split-Path -Parent $OutPath
if (-not (Test-Path -LiteralPath $outDir)) {
  New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

$wiringRows = @(Import-Csv -LiteralPath $WiringPath)
$boardRows = @(Import-Csv -LiteralPath $BoardValidationPath)
if ($wiringRows.Count -eq 0) {
  throw 'PL E-stop wiring evidence CSV has no rows'
}
if ($boardRows.Count -eq 0) {
  throw 'PL E-stop board validation CSV has no rows'
}

$wiringRequests = @($wiringRows | Where-Object { $_.evidence_state -eq 'pending' })
$boardRequests = @($boardRows | Where-Object { $_.evidence_state -ne 'board_verified' })
$doPwmRequests = @($wiringRequests | Where-Object { $_.signal_group -eq 'general_output' })
$busTxRequests = @($wiringRequests | Where-Object { $_.signal_group -eq 'bus_tx_gate' })

$wiringRel = Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $WiringPath
$boardRel = Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $BoardValidationPath
$outRel = Get-ProjectRelativeOutputPath -RootDir $ProjectDir -Path $OutPath
$evidenceRootRel = $EvidenceRootRelativePath

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# PL E-Stop Hardware Evidence Request')
$lines.Add('')
$lines.Add('This generated file is the field evidence request for promoting PL E-stop wiring from placeholder-only to real pins. Fill the CSV inputs, not this generated report.')
$lines.Add('')
$lines.Add('## Inputs To Fill')
$lines.Add('')
$lines.Add("- Wiring evidence CSV: ``$wiringRel``")
$lines.Add("- Board validation CSV: ``$boardRel``")
$lines.Add("- Evidence-file root: ``$evidenceRootRel``")
$lines.Add('')
$lines.Add('## Current Request State')
$lines.Add('')
$lines.Add('| Item | Count |')
$lines.Add('| --- | ---: |')
$lines.Add("| Wiring evidence requests | $($wiringRequests.Count) |")
$lines.Add("| Board validation requests | $($boardRequests.Count) |")
$lines.Add("| DO/PWM output requests | $($doPwmRequests.Count) |")
$lines.Add("| Bus TX gate requests | $($busTxRequests.Count) |")
$lines.Add('')
$lines.Add('## Wiring Evidence To Collect')
$lines.Add('')
$lines.Add('A row may move toward real RTL/XDC only after the CSV records real wiring, safe polarity, normal owner, PL gate point, and evidence paths. Connector/package evidence copied from XDC is only a candidate.')
$lines.Add('')
$lines.Add('| Signal | Group | Candidate | Connector | Collection Request | CSV Row Next Action |')
$lines.Add('| --- | --- | --- | --- | --- | --- |')
foreach ($row in $wiringRequests) {
  $candidateParts = @($row.candidate_net, $row.package_pin) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
  $candidate = ($candidateParts -join ' ')
  $lines.Add("| $(Escape-MarkdownCell -Value $row.signal_name) | $(Escape-MarkdownCell -Value $row.signal_group) | $(Escape-MarkdownCell -Value $candidate) | $(Escape-MarkdownCell -Value $row.connector) | $(Escape-MarkdownCell -Value (Get-WiringCollectionRequest -Row $row)) | $(Escape-MarkdownCell -Value $row.next_action) |")
}
$lines.Add('')
$lines.Add('## Board Measurements To Collect')
$lines.Add('')
$lines.Add('These tests are required after real wiring is connected. They are not satisfied by local RTL simulation, bitstream generation, or XSA export.')
$lines.Add('')
$lines.Add('| Test | Group | Trigger | Expected Result | Instrument | Acceptance | Next Action |')
$lines.Add('| --- | --- | --- | --- | --- | --- | --- |')
foreach ($row in $boardRequests) {
  $lines.Add("| $(Escape-MarkdownCell -Value $row.test_id) | $(Escape-MarkdownCell -Value $row.requirement_group) | $(Escape-MarkdownCell -Value $row.trigger) | $(Escape-MarkdownCell -Value $row.expected_result) | $(Escape-MarkdownCell -Value $row.required_instrument) | $(Escape-MarkdownCell -Value $row.acceptance_limit) | $(Escape-MarkdownCell -Value $row.next_action) |")
}
$lines.Add('')
$lines.Add('## Non-Negotiable Boundaries')
$lines.Add('')
$lines.Add('- DO1-DO14 and PWM1-PWM2 must not be promoted as ordinary outputs first; promotion must include the PL general-output gate, confirmed off polarity, AXI/status coverage, simulation update, and bench or board forced-off evidence.')
$lines.Add('- Bus gating must block only TX send-enable, driver-enable, TX idle, or TX_CTL while leaving the physical link, clock, power, and RX observation alive.')
$lines.Add('- Current production drive transport is EtherCAT over PS GEM1/EMIO; the current generated synth gate is locally verified on that PS GEM1/EMIO RGMII TX path, but BV10-BV12 board evidence is still required before claiming production bus cut capability.')
$lines.Add('- Any `board_verified` wiring or board-validation evidence file must be stored under `docs/evidence/pl_estop/` and referenced by project-relative path.')
$lines.Add('- Any `board_verified` evidence path must point to a non-placeholder `.md` evidence record with `Evidence State: board_verified` and the matching signal name or test ID; raw captures and logs are attachments referenced by that record.')
$lines.Add('- Each `board_verified` evidence record `Attachments:` line must list one or more existing comma- or semicolon-separated project-relative files under `docs/evidence/pl_estop/`; the record cannot list itself as an attachment.')
$lines.Add('- Until the CSV board evidence closes, the active E-stop input, DO/PWM outputs, and PS GEM1/EMIO RGMII TX gate remain local_verified_only; STO, brake, axis, and any additional bus TX owner stay fail-closed or unpromoted.')

$text = ($lines -join [Environment]::NewLine) + [Environment]::NewLine
if ($text -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
  throw 'Generated hardware evidence request contains an absolute path'
}
if ($text -match 'vivado_hw_project') {
  throw 'Generated hardware evidence request contains an old-project reference'
}

Write-TextFileWithRetry -Path $OutPath -Text $text

Write-Output 'pl_estop_hardware_evidence_request=open'
Write-Output "hardware_evidence_request_report=$outRel"
Write-Output "wiring_request_items=$($wiringRequests.Count)"
Write-Output "board_request_items=$($boardRequests.Count)"
Write-Output "do_pwm_request_items=$($doPwmRequests.Count)"
Write-Output "bus_tx_request_items=$($busTxRequests.Count)"
