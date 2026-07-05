param(
  [string]$ProjectDir,
  [string]$OutPath
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($OutPath)) {
  $OutPath = Join-Path $ProjectDir 'docs/pl_estop_field_packet.md'
}

. (Join-Path $PSScriptRoot 'write_text_with_retry.ps1')

$WiringCsvRelativePath = 'docs/pl_estop_wiring_evidence.csv'
$BoardCsvRelativePath = 'docs/pl_estop_board_validation_evidence.csv'
$EvidenceRootRelativePath = 'docs/evidence/pl_estop'

function Convert-ToProjectRelativePath {
  param(
    [string]$RootDir,
    [string]$Path
  )

  $root = (Resolve-Path -LiteralPath $RootDir).Path.TrimEnd('\', '/')
  if (Test-Path -LiteralPath $Path) {
    $full = (Resolve-Path -LiteralPath $Path).Path
  } else {
    $parent = Split-Path -Parent $Path
    $fullParent = (Resolve-Path -LiteralPath $parent).Path
    $full = Join-Path $fullParent (Split-Path -Leaf $Path)
  }
  if (-not $full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Path is outside project: $Path"
  }
  return ($full.Substring($root.Length).TrimStart('\', '/') -replace '\\', '/')
}

function Convert-MarkdownText {
  param([string]$Value)
  if ([string]::IsNullOrWhiteSpace($Value)) {
    return ''
  }
  return (($Value -replace '\\', '/') -replace '\|', '\|')
}

function Get-SafeFileStem {
  param([string]$Value)
  return ($Value -replace '[^A-Za-z0-9_-]', '_')
}

function Get-WiringEvidencePaths {
  param([string]$SignalName)
  $stem = Get-SafeFileStem -Value $SignalName
  return "wiring=$EvidenceRootRelativePath/wiring/$stem.md; bench=$EvidenceRootRelativePath/bench/$stem.md; board=$EvidenceRootRelativePath/board/$stem.md"
}

function Get-BoardEvidencePath {
  param([string]$TestId)
  $stem = Get-SafeFileStem -Value $TestId
  return "$EvidenceRootRelativePath/board_validation/$stem.md"
}

$wiringCsvPath = Join-Path $ProjectDir ($WiringCsvRelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
$boardCsvPath = Join-Path $ProjectDir ($BoardCsvRelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
$evidenceRootPath = Join-Path $ProjectDir ($EvidenceRootRelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)

foreach ($required in @($wiringCsvPath, $boardCsvPath, $evidenceRootPath)) {
  if (-not (Test-Path -LiteralPath $required)) {
    throw "Missing required field-packet input: $(Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $required)"
  }
}

$wiringRows = @(Import-Csv -LiteralPath $wiringCsvPath)
$boardRows = @(Import-Csv -LiteralPath $boardCsvPath)
if ($wiringRows.Count -eq 0) {
  throw 'Wiring evidence CSV has no rows'
}
if ($boardRows.Count -eq 0) {
  throw 'Board validation evidence CSV has no rows'
}

$doPwmRows = @($wiringRows | Where-Object { $_.signal_group -eq 'general_output' })
$busTxRows = @($wiringRows | Where-Object { $_.signal_group -eq 'bus_tx_gate' })
$pendingWiringRows = @($wiringRows | Where-Object { $_.evidence_state -ne 'board_verified' })
$pendingBoardRows = @($boardRows | Where-Object { $_.evidence_state -ne 'board_verified' })
$fieldPacketState = if ($pendingWiringRows.Count -eq 0 -and $pendingBoardRows.Count -eq 0) { 'closed' } else { 'open' }

$outDir = Split-Path -Parent $OutPath
if (-not (Test-Path -LiteralPath $outDir)) {
  New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}
$outRel = Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $OutPath

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# PL E-Stop Field Evidence Packet')
$lines.Add('')
$lines.Add('This generated packet is the field-facing intake checklist for PL E-stop wiring and board evidence. Fill the CSV inputs and evidence files; do not hand-edit this generated packet.')
$lines.Add('')
$lines.Add('## Inputs')
$lines.Add('')
$lines.Add("- Wiring CSV: ``$WiringCsvRelativePath``")
$lines.Add("- Board validation CSV: ``$BoardCsvRelativePath``")
$lines.Add("- Evidence-file root: ``$EvidenceRootRelativePath``")
$lines.Add("- Packet state: ``$fieldPacketState``")
$lines.Add('')
$lines.Add('## Counts')
$lines.Add('')
$lines.Add('| Item | Count |')
$lines.Add('| --- | ---: |')
$lines.Add("| Wiring rows | $($wiringRows.Count) |")
$lines.Add("| Wiring rows not board-verified | $($pendingWiringRows.Count) |")
$lines.Add("| Board validation rows | $($boardRows.Count) |")
$lines.Add("| Board validation rows not verified | $($pendingBoardRows.Count) |")
$lines.Add("| DO/PWM rows | $($doPwmRows.Count) |")
$lines.Add("| Bus TX rows | $($busTxRows.Count) |")
$lines.Add('')
$lines.Add('## Wiring CSV Intake Rows')
$lines.Add('')
$lines.Add('Each row must stay `pending` until real wiring is known. A row can move to `ready_for_rtl_xdc` only after polarity, normal owner, PL gate point, schematic evidence, and wiring evidence are filled. A row can move to `board_verified` only after bench and board evidence files exist under the evidence-file root.')
$lines.Add('')
$lines.Add('For `board_verified`, `bench_evidence` and `board_evidence` must point to non-placeholder `.md` evidence records. Raw scope captures, photos, logic-analyzer exports, or logs should be referenced from those `.md` records instead of being used as the CSV evidence path directly.')
$lines.Add('')
$lines.Add('| Signal | Group | Candidate | Connector | Required CSV fields before promotion | Suggested evidence paths | Next action |')
$lines.Add('| --- | --- | --- | --- | --- | --- | --- |')
foreach ($row in $wiringRows) {
  $candidate = ("$($row.candidate_net) $($row.package_pin)").Trim()
  $fields = 'polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence'
  $lines.Add("| $(Convert-MarkdownText $row.signal_name) | $(Convert-MarkdownText $row.signal_group) | $(Convert-MarkdownText $candidate) | $(Convert-MarkdownText $row.connector) | $fields | ``$(Get-WiringEvidencePaths -SignalName $row.signal_name)`` | $(Convert-MarkdownText $row.next_action) |")
}
$lines.Add('')
$lines.Add('## Board Validation Intake Rows')
$lines.Add('')
$lines.Add('Board validation rows stay `pending` until the real board or bench measurement file exists under the evidence-file root and the CSV row records measured value, operator, and date.')
$lines.Add('')
$lines.Add('For `board_verified`, `evidence_path` must point to a non-placeholder `.md` evidence record. Raw captures or logs should be referenced from that record.')
$lines.Add('')
$lines.Add('| Test | Group | Instrument | Acceptance | Suggested evidence_path | Next action |')
$lines.Add('| --- | --- | --- | --- | --- | --- |')
foreach ($row in $boardRows) {
  $lines.Add("| $(Convert-MarkdownText $row.test_id) | $(Convert-MarkdownText $row.requirement_group) | $(Convert-MarkdownText $row.required_instrument) | $(Convert-MarkdownText $row.acceptance_limit) | ``$(Get-BoardEvidencePath -TestId $row.test_id)`` | $(Convert-MarkdownText $row.next_action) |")
}
$lines.Add('')
$lines.Add('## Non-Negotiable Field Rules')
$lines.Add('')
$lines.Add('- Use only project-relative evidence paths. Do not put drive-letter absolute paths in CSV fields.')
$lines.Add('- Evidence files used by `board_verified` rows must be non-placeholder `.md` records under `docs/evidence/pl_estop/`; raw captures or logs are attachments referenced by the `.md` record.')
$lines.Add('- Each `board_verified` evidence record must contain `Evidence State: board_verified`, the matching signal name or test ID, operator, date, instrument, result, and attachment references.')
$lines.Add('- The `Attachments:` line in a `board_verified` evidence record must list one or more existing comma- or semicolon-separated project-relative files under `docs/evidence/pl_estop/`; the evidence record must not list itself as an attachment.')
$lines.Add('- Evidence records must not contain `TODO`, `TBD`, `PLACEHOLDER`, or `DRAFT` markers.')
$lines.Add('- DO1-DO14 and PWM1-PWM2 must be connected through `pl_estop_general_output_gate` after the normal output owner and must include confirmed off polarity before real pin promotion.')
$lines.Add('- Bus gating must block only TX send-enable, driver-enable, TX idle, or TX_CTL; it must not reset PHY, cut link clock or power, or disable RX/status observation.')
$lines.Add('- Current production drive transport is EtherCAT over PS GEM1/EMIO; the current generated synth gate is locally verified on that PS GEM1/EMIO RGMII TX path, but BV10-BV12 board evidence is still required before claiming production drive-bus safety.')
$lines.Add('- Until CSV board evidence closes, the active E-stop input, DO/PWM outputs, and PS GEM1/EMIO RGMII TX gate remain local_verified_only; STO, brake, axis, and any additional bus TX owner stay fail-closed or unpromoted.')
$lines.Add('')
$lines.Add('## Evidence Record Contract')
$lines.Add('')
$lines.Add('Each `.md` evidence record used by a `board_verified` CSV row must include these machine-checkable lines:')
$lines.Add('')
$lines.Add('```text')
$lines.Add('Evidence State: board_verified')
$lines.Add('Subject: <signal_name or test_id>')
$lines.Add('Operator: <person>')
$lines.Add('Date: <YYYY-MM-DD>')
$lines.Add('Instrument: <scope logic_analyzer ps_axi_readback photo log other>')
$lines.Add('Result: <measured result and pass/fail statement>')
$lines.Add('Attachments: <comma- or semicolon-separated project-relative files under docs/evidence/pl_estop/>')
$lines.Add('```')

$text = ($lines -join [Environment]::NewLine) + [Environment]::NewLine
if ($text -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
  throw 'Generated PL E-stop field packet contains an absolute path'
}
if ($text -match 'vivado_hw_project') {
  throw 'Generated PL E-stop field packet contains an old-project reference'
}

Write-TextFileWithRetry -Path $OutPath -Text $text

Write-Output "pl_estop_field_packet=$fieldPacketState"
Write-Output "field_packet_report=$outRel"
Write-Output "wiring_rows=$($wiringRows.Count)"
Write-Output "board_tests=$($boardRows.Count)"
Write-Output "do_pwm_rows=$($doPwmRows.Count)"
Write-Output "bus_tx_rows=$($busTxRows.Count)"
