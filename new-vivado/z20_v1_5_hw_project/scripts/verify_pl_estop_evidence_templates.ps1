param(
  [string]$ProjectDir,
  [string]$TemplatePath
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($TemplatePath)) {
  $TemplatePath = Join-Path $ProjectDir 'docs/pl_estop_evidence_record_templates.md'
}

$WiringCsvRelativePath = 'docs/pl_estop_wiring_evidence.csv'
$BoardCsvRelativePath = 'docs/pl_estop_board_validation_evidence.csv'
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

function Assert-TextContains {
  param(
    [string]$Text,
    [string]$Pattern,
    [string]$Label
  )

  if ($Text -notmatch $Pattern) {
    throw "Missing PL E-stop evidence-template content: $Label"
  }
}

function Get-SafeFileStem {
  param([string]$Value)
  return ($Value -replace '[^A-Za-z0-9_-]', '_')
}

function Get-WiringRecordPaths {
  param([string]$SignalName)
  $stem = Get-SafeFileStem -Value $SignalName
  return @(
    "$EvidenceRootRelativePath/wiring/$stem.md",
    "$EvidenceRootRelativePath/bench/$stem.md",
    "$EvidenceRootRelativePath/board/$stem.md"
  )
}

function Get-BoardRecordPath {
  param([string]$TestId)
  $stem = Get-SafeFileStem -Value $TestId
  return "$EvidenceRootRelativePath/board_validation/$stem.md"
}

if (-not (Test-Path -LiteralPath $TemplatePath)) {
  throw 'Missing docs/pl_estop_evidence_record_templates.md'
}

$wiringCsvPath = Join-Path $ProjectDir ($WiringCsvRelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
$boardCsvPath = Join-Path $ProjectDir ($BoardCsvRelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
$wiringRows = @(Import-Csv -LiteralPath $wiringCsvPath)
$boardRows = @(Import-Csv -LiteralPath $boardCsvPath)
$doPwmRows = @($wiringRows | Where-Object { $_.signal_group -eq 'general_output' })
$busTxRows = @($wiringRows | Where-Object { $_.signal_group -eq 'bus_tx_gate' })
$pendingWiringRows = @($wiringRows | Where-Object { $_.evidence_state -ne 'board_verified' })
$pendingBoardRows = @($boardRows | Where-Object { $_.evidence_state -ne 'board_verified' })
$templateState = if ($pendingWiringRows.Count -eq 0 -and $pendingBoardRows.Count -eq 0) { 'closed' } else { 'open' }

$text = Get-Content -LiteralPath $TemplatePath -Raw -Encoding UTF8
if ($text -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
  throw 'PL E-stop evidence templates contain an absolute path'
}
if ($text -match 'vivado_hw_project') {
  throw 'PL E-stop evidence templates contain an old-project reference'
}
if ($text -match '(?im)^\s*Evidence State:\s*board_verified\s*$') {
  throw 'PL E-stop evidence templates contain a board_verified record'
}

Assert-TextContains -Text $text -Pattern '# PL E-Stop Evidence Record Templates' -Label 'title'
$wiringCsvText = 'Wiring CSV: `' + $WiringCsvRelativePath + '`'
$boardCsvText = 'Board validation CSV: `' + $BoardCsvRelativePath + '`'
$evidenceRootText = 'Evidence-file root: `' + $EvidenceRootRelativePath + '`'
$templateStateText = 'Template state: `' + $templateState + '`'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($wiringCsvText)) -Label 'wiring CSV path'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($boardCsvText)) -Label 'board CSV path'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($evidenceRootText)) -Label 'evidence root path'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($templateStateText)) -Label 'template state'
Assert-TextContains -Text $text -Pattern 'Evidence State: pending_until_measured' -Label 'pending evidence state skeleton'
Assert-TextContains -Text $text -Pattern 'must not be referenced by a `board_verified` CSV row' -Label 'not proof warning'
Assert-TextContains -Text $text -Pattern 'Do not reference this generated template file from a CSV evidence path' -Label 'CSV path warning'
Assert-TextContains -Text $text -Pattern 'one or more existing comma- or semicolon-separated project-relative attachment files under `docs/evidence/pl_estop/`' -Label 'attachment existing-file instruction'
Assert-TextContains -Text $text -Pattern 'DO1` - `DO14` and `PWM1` - `PWM2` records must include confirmed load wiring' -Label 'DO/PWM template note'
Assert-TextContains -Text $text -Pattern 'Bus TX evidence must show TX is blocked while Link, clock, reset, power, RX, and status observation remain alive' -Label 'bus TX template note'

foreach ($row in $wiringRows) {
  Assert-TextContains -Text $text -Pattern ([regex]::Escape($row.signal_name)) -Label "wiring signal $($row.signal_name)"
  foreach ($path in (Get-WiringRecordPaths -SignalName $row.signal_name)) {
    Assert-TextContains -Text $text -Pattern ([regex]::Escape($path)) -Label "wiring evidence path $path"
  }
}
foreach ($row in $boardRows) {
  Assert-TextContains -Text $text -Pattern ([regex]::Escape($row.test_id)) -Label "board test $($row.test_id)"
  $path = Get-BoardRecordPath -TestId $row.test_id
  Assert-TextContains -Text $text -Pattern ([regex]::Escape($path)) -Label "board evidence path $path"
}

$templateRel = Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $TemplatePath
Write-Output 'pl_estop_evidence_templates_verify=ok'
Write-Output "evidence_templates_report=$templateRel"
Write-Output "template_state=$templateState"
Write-Output "wiring_templates=$($wiringRows.Count)"
Write-Output "board_validation_templates=$($boardRows.Count)"
Write-Output "do_pwm_templates=$($doPwmRows.Count)"
Write-Output "bus_tx_templates=$($busTxRows.Count)"
Write-Output 'board_verified_template_records=0'
