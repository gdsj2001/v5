param(
  [string]$ProjectDir,
  [string]$WiringPath,
  [string]$BoardValidationPath,
  [string]$RequestPath
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
if ([string]::IsNullOrWhiteSpace($RequestPath)) {
  $RequestPath = Join-Path $ProjectDir 'docs/pl_estop_hardware_evidence_request.md'
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

function Assert-TextContains {
  param(
    [string]$Text,
    [string]$Pattern,
    [string]$Label
  )

  if ($Text -notmatch $Pattern) {
    throw "Missing hardware evidence request content: $Label"
  }
}

foreach ($path in @($WiringPath, $BoardValidationPath, $RequestPath)) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing hardware evidence request input: $path"
  }
}

$requestText = Get-Content -LiteralPath $RequestPath -Raw -Encoding UTF8
if ($requestText -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
  throw 'Hardware evidence request contains an absolute path'
}
if ($requestText -match 'vivado_hw_project') {
  throw 'Hardware evidence request contains an old-project reference'
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
$requestState = if ($wiringRequests.Count -eq 0 -and $boardRequests.Count -eq 0) { 'closed' } else { 'open' }

$wiringRel = Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $WiringPath
$boardRel = Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $BoardValidationPath
$requestRel = Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $RequestPath
$wiringInputText = 'Wiring evidence CSV: `' + $wiringRel + '`'
$boardInputText = 'Board validation CSV: `' + $boardRel + '`'
$evidenceRootText = 'Evidence-file root: `' + $EvidenceRootRelativePath + '`'

Assert-TextContains -Text $requestText -Pattern '# PL E-Stop Hardware Evidence Request' -Label 'title'
Assert-TextContains -Text $requestText -Pattern ([regex]::Escape($wiringInputText)) -Label 'wiring input path'
Assert-TextContains -Text $requestText -Pattern ([regex]::Escape($boardInputText)) -Label 'board input path'
Assert-TextContains -Text $requestText -Pattern ([regex]::Escape($evidenceRootText)) -Label 'evidence root path'
Assert-TextContains -Text $requestText -Pattern ([regex]::Escape("| Wiring evidence requests | $($wiringRequests.Count) |")) -Label 'wiring request count'
Assert-TextContains -Text $requestText -Pattern ([regex]::Escape("| Board validation requests | $($boardRequests.Count) |")) -Label 'board request count'
Assert-TextContains -Text $requestText -Pattern ([regex]::Escape("| DO/PWM output requests | $($doPwmRequests.Count) |")) -Label 'DO/PWM request count'
Assert-TextContains -Text $requestText -Pattern ([regex]::Escape("| Bus TX gate requests | $($busTxRequests.Count) |")) -Label 'bus TX request count'
Assert-TextContains -Text $requestText -Pattern 'DO1-DO14 and PWM1-PWM2 must not be promoted as ordinary outputs first' -Label 'DO/PWM boundary'
Assert-TextContains -Text $requestText -Pattern 'Bus gating must block only TX send-enable' -Label 'bus TX boundary'
Assert-TextContains -Text $requestText -Pattern 'current generated synth gate is locally verified on that PS GEM1/EMIO RGMII TX path' -Label 'production EtherCAT local gate boundary'
Assert-TextContains -Text $requestText -Pattern 'BV10-BV12 board evidence is still required' -Label 'production EtherCAT board evidence boundary'
Assert-TextContains -Text $requestText -Pattern 'Any `board_verified` wiring or board-validation evidence file must be stored under `docs/evidence/pl_estop/`' -Label 'evidence root boundary'
Assert-TextContains -Text $requestText -Pattern 'non-placeholder `\.md` evidence record with `Evidence State: board_verified`' -Label 'evidence record contract boundary'
Assert-TextContains -Text $requestText -Pattern 'Attachments:` line must list one or more existing comma- or semicolon-separated project-relative files under `docs/evidence/pl_estop/`' -Label 'evidence attachment existing-file contract'
Assert-TextContains -Text $requestText -Pattern 'active E-stop input, DO/PWM outputs, and PS GEM1/EMIO RGMII TX gate remain local_verified_only' -Label 'local-only active safety boundary'
Assert-TextContains -Text $requestText -Pattern 'STO, brake, axis, and any additional bus TX owner stay fail-closed or unpromoted' -Label 'fail-closed unpromoted boundary'

foreach ($row in $wiringRequests) {
  $linePattern = "\|\s*$([regex]::Escape($row.signal_name))\s*\|\s*$([regex]::Escape($row.signal_group))\s*\|"
  Assert-TextContains -Text $requestText -Pattern $linePattern -Label "wiring row $($row.signal_name)"
}

foreach ($row in $boardRequests) {
  $linePattern = "\|\s*$([regex]::Escape($row.test_id))\s*\|\s*$([regex]::Escape($row.requirement_group))\s*\|"
  Assert-TextContains -Text $requestText -Pattern $linePattern -Label "board row $($row.test_id)"
}

Write-Output 'pl_estop_hardware_evidence_request_verify=ok'
Write-Output "hardware_evidence_request_report=$requestRel"
Write-Output "hardware_evidence_request_state=$requestState"
Write-Output "wiring_request_items=$($wiringRequests.Count)"
Write-Output "board_request_items=$($boardRequests.Count)"
Write-Output "do_pwm_request_items=$($doPwmRequests.Count)"
Write-Output "bus_tx_request_items=$($busTxRequests.Count)"
