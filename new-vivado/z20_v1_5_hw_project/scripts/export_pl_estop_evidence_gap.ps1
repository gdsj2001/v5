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
  $OutPath = Join-Path $ProjectDir 'docs/pl_estop_evidence_gap.md'
}

. (Join-Path $PSScriptRoot 'write_text_with_retry.ps1')

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

function Escape-MarkdownCell {
  param([string]$Value)
  if ([string]::IsNullOrWhiteSpace($Value)) {
    return ''
  }
  return (($Value -replace '\|', '/') -replace '\r?\n', ' ')
}

function Get-MissingWiringFields {
  param($Row)

  $missing = New-Object System.Collections.Generic.List[string]
  foreach ($field in @('polarity_or_safe_level', 'normal_owner', 'pl_gate_point', 'wiring_evidence')) {
    if ([string]::IsNullOrWhiteSpace($Row.$field)) {
      $missing.Add($field)
    }
  }
  if ([string]::IsNullOrWhiteSpace($Row.schematic_evidence)) {
    $missing.Add('schematic_evidence')
  }
  return ($missing -join ', ')
}

function Get-MissingBoardFields {
  param($Row)

  $missing = New-Object System.Collections.Generic.List[string]
  foreach ($field in @('measured_value', 'evidence_path', 'operator', 'date')) {
    if ([string]::IsNullOrWhiteSpace($Row.$field)) {
      $missing.Add($field)
    }
  }
  return ($missing -join ', ')
}

foreach ($path in @($WiringPath, $BoardValidationPath)) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing evidence input: $path"
  }
}

$wiringRows = @(Import-Csv -LiteralPath $WiringPath)
$boardRows = @(Import-Csv -LiteralPath $BoardValidationPath)
if ($wiringRows.Count -eq 0) {
  throw 'PL E-stop wiring evidence CSV has no rows'
}
if ($boardRows.Count -eq 0) {
  throw 'PL E-stop board validation CSV has no rows'
}

$pendingWiring = @($wiringRows | Where-Object { $_.evidence_state -eq 'pending' })
$readyWiring = @($wiringRows | Where-Object { $_.evidence_state -ne 'pending' })
$pendingBoard = @($boardRows | Where-Object { $_.evidence_state -eq 'pending' })
$verifiedBoard = @($boardRows | Where-Object { $_.evidence_state -eq 'board_verified' })

$outDir = Split-Path -Parent $OutPath
if (-not (Test-Path -LiteralPath $outDir)) {
  New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

$wiringRel = Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $WiringPath
$boardRel = Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $BoardValidationPath
$outRel = if (Test-Path -LiteralPath $OutPath) {
  Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $OutPath
} else {
  $root = (Resolve-Path -LiteralPath $ProjectDir).Path.TrimEnd('\', '/')
  $fullParent = (Resolve-Path -LiteralPath $outDir).Path
  $candidate = Join-Path $fullParent (Split-Path -Leaf $OutPath)
  (($candidate.Substring($root.Length).TrimStart('\', '/')) -replace '\\', '/')
}

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# PL E-Stop Evidence Gap')
$lines.Add('')
$lines.Add('This report is generated from project-local evidence CSV files. It is a handoff checklist, not board proof.')
$lines.Add('')
$lines.Add('## Inputs')
$lines.Add('')
$lines.Add("- Wiring evidence: ``$wiringRel``")
$lines.Add("- Board validation evidence: ``$boardRel``")
$lines.Add('')
$lines.Add('## Current Gate State')
$lines.Add('')
$lines.Add("| Gate | Ready | Pending | Required |")
$lines.Add("| --- | ---: | ---: | ---: |")
$lines.Add("| Wiring evidence rows | $($readyWiring.Count) | $($pendingWiring.Count) | $($wiringRows.Count) |")
$lines.Add("| Board validation tests | $($verifiedBoard.Count) | $($pendingBoard.Count) | $($boardRows.Count) |")
$lines.Add('')
$lines.Add('## Wiring Evidence Gaps')
$lines.Add('')
$lines.Add('Rows below must not be promoted to active XDC or real PL outputs until the missing fields are backed by actual wiring evidence.')
$lines.Add('')
$lines.Add('| Signal | Group | Candidate | Pin | Connector | Missing Before ready_for_rtl_xdc | Next Action |')
$lines.Add('| --- | --- | --- | --- | --- | --- | --- |')
foreach ($row in $pendingWiring) {
  $candidate = $row.candidate_net
  if (-not [string]::IsNullOrWhiteSpace($row.package_pin)) {
    $candidate = "$candidate $($row.package_pin)".Trim()
  }
  $missingFields = Get-MissingWiringFields -Row $row
  $lines.Add("| $(Escape-MarkdownCell -Value $row.signal_name) | $(Escape-MarkdownCell -Value $row.signal_group) | $(Escape-MarkdownCell -Value $candidate) | $(Escape-MarkdownCell -Value $row.package_pin) | $(Escape-MarkdownCell -Value $row.connector) | $(Escape-MarkdownCell -Value $missingFields) | $(Escape-MarkdownCell -Value $row.next_action) |")
}
$lines.Add('')
$lines.Add('## Board Validation Gaps')
$lines.Add('')
$lines.Add('Rows below need bench or board measurements after real wiring is connected. Keeping them pending is the correct fail-closed state until then.')
$lines.Add('')
$lines.Add('| Test | Group | Instrument | Acceptance | Missing Before board_verified | Next Action |')
$lines.Add('| --- | --- | --- | --- | --- | --- |')
foreach ($row in $pendingBoard) {
  $missingFields = Get-MissingBoardFields -Row $row
  $lines.Add("| $(Escape-MarkdownCell -Value $row.test_id) | $(Escape-MarkdownCell -Value $row.requirement_group) | $(Escape-MarkdownCell -Value $row.required_instrument) | $(Escape-MarkdownCell -Value $row.acceptance_limit) | $(Escape-MarkdownCell -Value $missingFields) | $(Escape-MarkdownCell -Value $row.next_action) |")
}
$lines.Add('')
$lines.Add('## Fail-Closed Notes')
$lines.Add('')
$lines.Add('- XDC connector/package evidence is not enough to connect a real safety input or output.')
$lines.Add('- DO1-DO14 and PWM1-PWM2 must stay behind the PL E-stop general-output gate when promoted.')
$lines.Add('- The bus TX gate may only target a confirmed TX send-enable, driver-enable, TX idle, or TX_CTL path; it must not break PHY reset, link clock, link power, or RX observation.')
$lines.Add('- Current production drive transport is EtherCAT over PS GEM1/EMIO; the current generated synth gate is locally verified on that PS GEM1/EMIO RGMII TX path, but BV10-BV12 board evidence is still required before claiming the production EtherCAT bus can be cut safely by PL.')

$text = ($lines -join [Environment]::NewLine) + [Environment]::NewLine
if ($text -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
  throw 'Generated evidence gap report contains an absolute path'
}

Write-TextFileWithRetry -Path $OutPath -Text $text

Write-Output 'pl_estop_evidence_gap=open'
Write-Output "evidence_gap_report=$outRel"
Write-Output "wiring_pending_rows=$($pendingWiring.Count)"
Write-Output "board_pending_tests=$($pendingBoard.Count)"
Write-Output "wiring_ready_rows=$($readyWiring.Count)"
Write-Output "board_verified_tests=$($verifiedBoard.Count)"
