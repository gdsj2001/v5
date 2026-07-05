param(
  [string]$ProjectDir
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

function Convert-KeyValueOutput {
  param([string[]]$Lines)

  $result = [ordered]@{}
  foreach ($line in $Lines) {
    $matches = [regex]::Matches($line, '(?<key>[A-Za-z0-9_]+)=(?<value>.*?)(?=\s+[A-Za-z0-9_]+=|$)')
    foreach ($match in $matches) {
      $result[$match.Groups['key'].Value] = $match.Groups['value'].Value
    }
  }
  return $result
}

function Invoke-ProjectScript {
  param(
    [string]$RootDir,
    [string]$ScriptRelativePath
  )

  $scriptPath = Join-Path $RootDir ($ScriptRelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
  if (-not (Test-Path -LiteralPath $scriptPath)) {
    throw "Missing script: $ScriptRelativePath"
  }
  $output = @(& powershell -NoProfile -ExecutionPolicy Bypass -File $scriptPath -ProjectDir $RootDir)
  if ($LASTEXITCODE -ne 0) {
    throw "$ScriptRelativePath failed: $($output -join '; ')"
  }
  return Convert-KeyValueOutput -Lines $output
}

$active = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/check_active_xdc.ps1'
$independence = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_project_independence.ps1'
$safety = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_safety_boundary.ps1'
$wiring = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_wiring_evidence.ps1'
$boardValidation = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_board_validation.ps1'
$hardwareRequestVerify = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_hardware_evidence_request.ps1'
$evidenceTemplatesVerify = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_evidence_templates.ps1'
$fieldIntake = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_field_intake.ps1'
$remaining = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_remaining_drc_ports.ps1'
$conflicts = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/export_active_pin_conflicts.ps1'

$blockers = New-Object System.Collections.Generic.List[string]

if ($independence.project_independence -ne 'ok' -or $independence.project_path_relative -ne 'ok') {
  $blockers.Add('project_independence_not_ok')
}
if ($active.unassigned_top_ports_count -ne '0') {
  $blockers.Add('active_xdc_unassigned_ports')
}
if ($remaining.csv_rows -ne '0' -or $remaining.csv_matches_check_active -ne 'yes') {
  $blockers.Add('remaining_drc_ports_not_closed')
}
if ($conflicts.active_pin_conflicts -ne '0') {
  $blockers.Add('active_pin_conflicts_present')
}
if ($safety.pl_estop_safety_boundary -ne 'ok') {
  $blockers.Add('pl_estop_safety_boundary_not_ok')
}
if ($safety.do_pwm_gate -ne 'top_hard_gate_local_unverified' -or
    $safety.bus_tx_gate -ne 'top_rgmii_tx_gate_local_unverified') {
  $blockers.Add('pl_estop_hard_gate_boundary_not_ok')
}
if ($hardwareRequestVerify.pl_estop_hardware_evidence_request_verify -ne 'ok') {
  $blockers.Add('pl_estop_hardware_evidence_request_verify_not_ok')
}
if ($evidenceTemplatesVerify.pl_estop_evidence_templates_verify -ne 'ok' -or
    $evidenceTemplatesVerify.board_verified_template_records -ne '0') {
  $blockers.Add('pl_estop_evidence_templates_verify_not_ok')
}
if ($fieldIntake.field_intake_structural_contract -ne 'ok') {
  $blockers.Add('pl_estop_field_intake_structural_contract_not_ok')
}
if ($fieldIntake.field_runbook_verify -ne 'ok' -or
    $fieldIntake.field_runbook_state -ne 'open') {
  $blockers.Add('pl_estop_field_runbook_verify_not_ok')
}
if ($wiring.ready_for_real_pins -ne 'yes') {
  $blockers.Add('pl_estop_wiring_evidence_not_ready')
}
if ($wiring.board_evidence_ready -ne 'yes') {
  $blockers.Add('pl_estop_board_evidence_not_ready')
}
if ($boardValidation.board_validation_ready -ne 'yes') {
  $blockers.Add('pl_estop_board_validation_not_ready')
}

$e11Ready = (
  $independence.project_independence -eq 'ok' -and
  $independence.project_path_relative -eq 'ok' -and
  $active.unassigned_top_ports_count -eq '0' -and
  $remaining.csv_rows -eq '0' -and
  $remaining.csv_matches_check_active -eq 'yes' -and
  $conflicts.active_pin_conflicts -eq '0' -and
  $safety.pl_estop_safety_boundary -eq 'ok' -and
  $hardwareRequestVerify.pl_estop_hardware_evidence_request_verify -eq 'ok' -and
  $evidenceTemplatesVerify.pl_estop_evidence_templates_verify -eq 'ok' -and
  $evidenceTemplatesVerify.board_verified_template_records -eq '0' -and
  $fieldIntake.field_intake_structural_contract -eq 'ok' -and
  $fieldIntake.field_runbook_verify -eq 'ok' -and
  $fieldIntake.field_runbook_state -eq 'open' -and
  $wiring.ready_for_real_pins -eq 'yes'
)

$a11Ready = (
  $e11Ready -and
  $fieldIntake.pl_estop_field_intake -eq 'ready_for_board_slice' -and
  $wiring.board_evidence_ready -eq 'yes' -and
  $boardValidation.board_validation_ready -eq 'yes'
)

$readiness = 'not_ready'
if ($a11Ready) {
  $readiness = 'ready_for_board_validation'
} elseif ($e11Ready) {
  $readiness = 'ready_for_rtl_xdc'
}

Write-Output "pl_estop_readiness=$readiness"
Write-Output "e11_rtl_xdc_ready=$(if ($e11Ready) { 'yes' } else { 'no' })"
Write-Output "a11_board_validation_ready=$(if ($a11Ready) { 'yes' } else { 'no' })"
Write-Output "readiness_blockers=$($blockers -join ',')"
Write-Output "project_independence=$($independence.project_independence)"
Write-Output "unassigned_top_ports_count=$($active.unassigned_top_ports_count)"
Write-Output "remaining_drc_rows=$($remaining.csv_rows)"
Write-Output "active_pin_conflicts=$($conflicts.active_pin_conflicts)"
Write-Output "pl_estop_safety_boundary=$($safety.pl_estop_safety_boundary)"
Write-Output "pl_estop_hardware_evidence_request_verify=$($hardwareRequestVerify.pl_estop_hardware_evidence_request_verify)"
Write-Output "hardware_evidence_request_state=$($hardwareRequestVerify.hardware_evidence_request_state)"
Write-Output "pl_estop_evidence_templates_verify=$($evidenceTemplatesVerify.pl_estop_evidence_templates_verify)"
Write-Output "evidence_templates_state=$($evidenceTemplatesVerify.template_state)"
Write-Output "board_verified_template_records=$($evidenceTemplatesVerify.board_verified_template_records)"
Write-Output "pl_estop_field_runbook_verify=$($fieldIntake.field_runbook_verify)"
Write-Output "field_runbook_state=$($fieldIntake.field_runbook_state)"
Write-Output "pl_estop_field_intake=$($fieldIntake.pl_estop_field_intake)"
Write-Output "field_intake_structural_contract=$($fieldIntake.field_intake_structural_contract)"
Write-Output "pl_estop_wiring_evidence=$($wiring.pl_estop_wiring_evidence)"
Write-Output "pl_estop_board_validation=$($boardValidation.pl_estop_board_validation)"
Write-Output "ready_for_real_pins=$($wiring.ready_for_real_pins)"
Write-Output "board_evidence_ready=$($wiring.board_evidence_ready)"
Write-Output "board_validation_ready=$($boardValidation.board_validation_ready)"
