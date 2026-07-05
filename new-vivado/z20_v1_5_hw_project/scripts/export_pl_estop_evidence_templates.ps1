param(
  [string]$ProjectDir,
  [string]$OutPath
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($OutPath)) {
  $OutPath = Join-Path $ProjectDir 'docs/pl_estop_evidence_record_templates.md'
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

function Get-WiringRecordPaths {
  param([string]$SignalName)
  $stem = Get-SafeFileStem -Value $SignalName
  return [ordered]@{
    wiring = "$EvidenceRootRelativePath/wiring/$stem.md"
    bench = "$EvidenceRootRelativePath/bench/$stem.md"
    board = "$EvidenceRootRelativePath/board/$stem.md"
  }
}

function Get-BoardRecordPath {
  param([string]$TestId)
  $stem = Get-SafeFileStem -Value $TestId
  return "$EvidenceRootRelativePath/board_validation/$stem.md"
}

$wiringCsvPath = Join-Path $ProjectDir ($WiringCsvRelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
$boardCsvPath = Join-Path $ProjectDir ($BoardCsvRelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
$evidenceRootPath = Join-Path $ProjectDir ($EvidenceRootRelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)

foreach ($required in @($wiringCsvPath, $boardCsvPath, $evidenceRootPath)) {
  if (-not (Test-Path -LiteralPath $required)) {
    throw "Missing required evidence-template input: $(Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $required)"
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
$templateState = if ($pendingWiringRows.Count -eq 0 -and $pendingBoardRows.Count -eq 0) { 'closed' } else { 'open' }

$outDir = Split-Path -Parent $OutPath
if (-not (Test-Path -LiteralPath $outDir)) {
  New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}
$outRel = Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $OutPath

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# PL E-Stop Evidence Record Templates')
$lines.Add('')
$lines.Add('This generated file gives field operators copy-ready `.md` evidence-record skeletons. It is not measurement evidence and must not be referenced by a `board_verified` CSV row.')
$lines.Add('')
$lines.Add('## Inputs')
$lines.Add('')
$lines.Add("- Wiring CSV: ``$WiringCsvRelativePath``")
$lines.Add("- Board validation CSV: ``$BoardCsvRelativePath``")
$lines.Add("- Evidence-file root: ``$EvidenceRootRelativePath``")
$lines.Add("- Template state: ``$templateState``")
$lines.Add('')
$lines.Add('## Counts')
$lines.Add('')
$lines.Add('| Item | Count |')
$lines.Add('| --- | ---: |')
$lines.Add("| Wiring templates | $($wiringRows.Count) |")
$lines.Add("| Board validation templates | $($boardRows.Count) |")
$lines.Add("| DO/PWM templates | $($doPwmRows.Count) |")
$lines.Add("| Bus TX templates | $($busTxRows.Count) |")
$lines.Add('')
$lines.Add('## How To Use')
$lines.Add('')
$lines.Add('- Create real evidence records under the suggested project-relative path, then fill real operator/date/instrument/result/attachment values.')
$lines.Add('- Keep `Evidence State: pending_until_measured` while the record is only prepared or partially filled.')
$lines.Add('- Change the evidence record state to the board-verified state only after the corresponding CSV row is changed to `board_verified` and the evidence is complete.')
$lines.Add('- Do not reference this generated template file from a CSV evidence path.')
$lines.Add('- Do not store drive-letter absolute paths in records or attachments.')
$lines.Add('- For a `board_verified` record, list one or more existing comma- or semicolon-separated project-relative attachment files under `docs/evidence/pl_estop/`; do not list the evidence record itself as an attachment.')
$lines.Add('')
$lines.Add('## Wiring Record Templates')
$lines.Add('')
$lines.Add('| Signal | Group | Wiring record | Bench record | Board record | Required CSV fields |')
$lines.Add('| --- | --- | --- | --- | --- | --- |')
foreach ($row in $wiringRows) {
  $paths = Get-WiringRecordPaths -SignalName $row.signal_name
  $fields = 'polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence'
  $lines.Add("| $(Convert-MarkdownText $row.signal_name) | $(Convert-MarkdownText $row.signal_group) | ``$($paths.wiring)`` | ``$($paths.bench)`` | ``$($paths.board)`` | $fields |")
}
$lines.Add('')
$lines.Add('## Board Validation Record Templates')
$lines.Add('')
$lines.Add('| Test | Group | Evidence record | Instrument | Acceptance |')
$lines.Add('| --- | --- | --- | --- | --- |')
foreach ($row in $boardRows) {
  $path = Get-BoardRecordPath -TestId $row.test_id
  $lines.Add("| $(Convert-MarkdownText $row.test_id) | $(Convert-MarkdownText $row.requirement_group) | ``$path`` | $(Convert-MarkdownText $row.required_instrument) | $(Convert-MarkdownText $row.acceptance_limit) |")
}
$lines.Add('')
$lines.Add('## Record Skeleton')
$lines.Add('')
$lines.Add('Use this skeleton for each real evidence file. The subject must exactly match a `signal_name` or `test_id` from the CSV row that will reference it.')
$lines.Add('')
$lines.Add('```text')
$lines.Add('Evidence State: pending_until_measured')
$lines.Add('Subject: <signal_name or test_id>')
$lines.Add('Operator: <person>')
$lines.Add('Date: <YYYY-MM-DD>')
$lines.Add('Instrument: <scope logic_analyzer ps_axi_readback photo log other>')
$lines.Add('Result: <measured result and pass/fail statement>')
$lines.Add('Attachments: <comma- or semicolon-separated project-relative files under docs/evidence/pl_estop/>')
$lines.Add('Notes: <short measurement context>')
$lines.Add('```')
$lines.Add('')
$lines.Add('## DO/PWM and Bus-Specific Notes')
$lines.Add('')
$lines.Add('- `DO1` - `DO14` and `PWM1` - `PWM2` records must include confirmed load wiring, off polarity, and proof that the signal is driven through `pl_estop_general_output_gate` before the physical pin.')
$lines.Add('- Bus TX records must name the real PL-owned bus owner, the gated send-enable or driver-enable signal, the idle/off polarity, and the queue flush or invalidate owner.')
$lines.Add('- Bus TX evidence must show TX is blocked while Link, clock, reset, power, RX, and status observation remain alive.')

$text = ($lines -join [Environment]::NewLine) + [Environment]::NewLine
if ($text -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
  throw 'Generated PL E-stop evidence templates contain an absolute path'
}
if ($text -match 'vivado_hw_project') {
  throw 'Generated PL E-stop evidence templates contain an old-project reference'
}
if ($text -match '(?im)^\s*Evidence State:\s*board_verified\s*$') {
  throw 'Generated PL E-stop evidence templates must not contain a board_verified record'
}

Write-TextFileWithRetry -Path $OutPath -Text $text

Write-Output "pl_estop_evidence_templates=$templateState"
Write-Output "evidence_templates_report=$outRel"
Write-Output "wiring_templates=$($wiringRows.Count)"
Write-Output "board_validation_templates=$($boardRows.Count)"
Write-Output "do_pwm_templates=$($doPwmRows.Count)"
Write-Output "bus_tx_templates=$($busTxRows.Count)"
Write-Output 'board_verified_template_records=0'
