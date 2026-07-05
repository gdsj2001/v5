param(
  [string]$ProjectDir,
  [string]$RunbookPath
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($RunbookPath)) {
  $RunbookPath = Join-Path $ProjectDir 'docs/pl_estop_field_execution_runbook.md'
}

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

function Assert-Contains {
  param(
    [string]$Text,
    [string]$Pattern,
    [string]$Label
  )

  if ($Text -notmatch $Pattern) {
    throw "Missing field runbook content: $Label"
  }
}

if (-not (Test-Path -LiteralPath $RunbookPath)) {
  throw 'Missing docs/pl_estop_field_execution_runbook.md'
}

$text = Get-Content -LiteralPath $RunbookPath -Raw -Encoding UTF8
if ($text -match '(?<![A-Za-z])[A-Za-z]:[\\/]' -or $text -match 'vivado_hw_project') {
  throw 'Field runbook contains an absolute path or old-project reference'
}

Assert-Contains -Text $text -Pattern '# PL E-Stop Field Execution Runbook' -Label 'title'
Assert-Contains -Text $text -Pattern 'Current board closure state: `local_verified_only`' -Label 'local-only state'
Assert-Contains -Text $text -Pattern 'docs/pl_estop_hardware_evidence_request\.md' -Label 'hardware evidence request'
Assert-Contains -Text $text -Pattern 'docs/pl_estop_field_packet\.md' -Label 'field packet'
Assert-Contains -Text $text -Pattern 'docs/pl_estop_wiring_evidence\.csv' -Label 'wiring CSV'
Assert-Contains -Text $text -Pattern 'docs/pl_estop_board_validation_evidence\.csv' -Label 'board validation CSV'
Assert-Contains -Text $text -Pattern 'docs/pl_estop_evidence_record_templates\.md' -Label 'evidence templates'
Assert-Contains -Text $text -Pattern 'docs/evidence/pl_estop/' -Label 'evidence root'
Assert-Contains -Text $text -Pattern 'scripts/verify_pl_estop_field_intake\.ps1' -Label 'field intake gate'
Assert-Contains -Text $text -Pattern 'scripts/verify_pl_estop_readiness\.ps1' -Label 'readiness gate'
Assert-Contains -Text $text -Pattern 'DO1` - `DO14` and `PWM1` - `PWM2`' -Label 'DO/PWM scope'
Assert-Contains -Text $text -Pattern 'pl_estop_general_output_gate' -Label 'DO/PWM gate'
Assert-Contains -Text $text -Pattern 'pl_estop_bus_tx_gate' -Label 'bus TX gate'
Assert-Contains -Text $text -Pattern 'TX send-enable, driver-enable, TX idle, or TX_CTL' -Label 'allowed bus gate signals'
Assert-Contains -Text $text -Pattern 'Keep the physical link alive' -Label 'link preservation'
Assert-Contains -Text $text -Pattern 'flushed or invalidated' -Label 'stale TX handling'
Assert-Contains -Text $text -Pattern 'EtherCAT over PS GEM1/EMIO' -Label 'current drive boundary'
Assert-Contains -Text $text -Pattern 'e11_rtl_xdc_ready=yes' -Label 'E11 promotion gate'
Assert-Contains -Text $text -Pattern 'a11_board_validation_ready=yes' -Label 'A11 board gate'
Assert-Contains -Text $text -Pattern 'XADC dedicated analog pins `L11/M12`' -Label 'XADC dedicated analog pin guard'
Assert-Contains -Text $text -Pattern 'reintroduce `U10`, `U9`, `AA12`, or `AB12` as ADC SPI pins' -Label 'retired ADC SPI pin guard'

$rel = Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $RunbookPath
Write-Output 'pl_estop_field_runbook_verify=ok'
Write-Output 'field_runbook_state=open'
Write-Output "field_runbook=$rel"
