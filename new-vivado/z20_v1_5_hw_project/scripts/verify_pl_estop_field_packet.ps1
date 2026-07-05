param(
  [string]$ProjectDir,
  [string]$PacketPath
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($PacketPath)) {
  $PacketPath = Join-Path $ProjectDir 'docs/pl_estop_field_packet.md'
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
    throw "Missing PL E-stop field packet content: $Label"
  }
}

if (-not (Test-Path -LiteralPath $PacketPath)) {
  throw 'Missing docs/pl_estop_field_packet.md'
}

$wiringCsvPath = Join-Path $ProjectDir ($WiringCsvRelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
$boardCsvPath = Join-Path $ProjectDir ($BoardCsvRelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
$wiringRows = @(Import-Csv -LiteralPath $wiringCsvPath)
$boardRows = @(Import-Csv -LiteralPath $boardCsvPath)
$doPwmRows = @($wiringRows | Where-Object { $_.signal_group -eq 'general_output' })
$busTxRows = @($wiringRows | Where-Object { $_.signal_group -eq 'bus_tx_gate' })
$pendingWiringRows = @($wiringRows | Where-Object { $_.evidence_state -ne 'board_verified' })
$pendingBoardRows = @($boardRows | Where-Object { $_.evidence_state -ne 'board_verified' })
$fieldPacketState = if ($pendingWiringRows.Count -eq 0 -and $pendingBoardRows.Count -eq 0) { 'closed' } else { 'open' }

$text = Get-Content -LiteralPath $PacketPath -Raw -Encoding UTF8
if ($text -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
  throw 'PL E-stop field packet contains an absolute path'
}
if ($text -match 'vivado_hw_project') {
  throw 'PL E-stop field packet contains an old-project reference'
}

Assert-TextContains -Text $text -Pattern '# PL E-Stop Field Evidence Packet' -Label 'title'
$wiringCsvText = 'Wiring CSV: `' + $WiringCsvRelativePath + '`'
$boardCsvText = 'Board validation CSV: `' + $BoardCsvRelativePath + '`'
$evidenceRootText = 'Evidence-file root: `' + $EvidenceRootRelativePath + '`'
$packetStateText = 'Packet state: `' + $fieldPacketState + '`'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($wiringCsvText)) -Label 'wiring CSV path'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($boardCsvText)) -Label 'board CSV path'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($evidenceRootText)) -Label 'evidence root path'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($packetStateText)) -Label 'packet state'
Assert-TextContains -Text $text -Pattern 'DO1-DO14 and PWM1-PWM2 must be connected through `pl_estop_general_output_gate`' -Label 'DO/PWM forced-off field rule'
Assert-TextContains -Text $text -Pattern 'Bus gating must block only TX send-enable, driver-enable, TX idle, or TX_CTL' -Label 'bus TX link-preserving field rule'
Assert-TextContains -Text $text -Pattern 'current generated synth gate is locally verified on that PS GEM1/EMIO RGMII TX path' -Label 'production bus local gate boundary'
Assert-TextContains -Text $text -Pattern 'BV10-BV12 board evidence is still required' -Label 'production bus board evidence boundary'
Assert-TextContains -Text $text -Pattern 'the active E-stop input, DO/PWM outputs, and PS GEM1/EMIO RGMII TX gate remain local_verified_only' -Label 'local-only active safety boundary'
Assert-TextContains -Text $text -Pattern 'STO, brake, axis, and any additional bus TX owner stay fail-closed or unpromoted' -Label 'unpromoted safety boundary'
Assert-TextContains -Text $text -Pattern 'Evidence State: board_verified' -Label 'evidence record state contract'
Assert-TextContains -Text $text -Pattern 'Subject: <signal_name or test_id>' -Label 'evidence record subject contract'
Assert-TextContains -Text $text -Pattern 'one or more existing comma- or semicolon-separated project-relative files under `docs/evidence/pl_estop/`' -Label 'evidence attachment existing-file contract'
Assert-TextContains -Text $text -Pattern 'Evidence records must not contain `TODO`, `TBD`, `PLACEHOLDER`, or `DRAFT` markers' -Label 'evidence placeholder ban'

foreach ($row in $wiringRows) {
  Assert-TextContains -Text $text -Pattern ([regex]::Escape($row.signal_name)) -Label "wiring row $($row.signal_name)"
}
foreach ($row in $boardRows) {
  Assert-TextContains -Text $text -Pattern ([regex]::Escape($row.test_id)) -Label "board validation row $($row.test_id)"
}

$packetRel = Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $PacketPath
Write-Output 'pl_estop_field_packet_verify=ok'
Write-Output "field_packet_report=$packetRel"
Write-Output "field_packet_state=$fieldPacketState"
Write-Output "wiring_rows=$($wiringRows.Count)"
Write-Output "board_tests=$($boardRows.Count)"
Write-Output "do_pwm_rows=$($doPwmRows.Count)"
Write-Output "bus_tx_rows=$($busTxRows.Count)"
