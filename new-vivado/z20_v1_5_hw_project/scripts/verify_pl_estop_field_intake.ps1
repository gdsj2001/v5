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

$hardwareRequest = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_hardware_evidence_request.ps1'
$fieldPacket = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_field_packet.ps1'
$fieldRunbook = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_field_runbook.ps1'
$evidenceTemplates = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_evidence_templates.ps1'
$evidenceRoot = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_evidence_root.ps1'
$wiring = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_wiring_evidence.ps1'
$boardValidation = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_board_validation.ps1'
$safety = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_safety_boundary.ps1'

$blockers = New-Object System.Collections.Generic.List[string]

if ($hardwareRequest.pl_estop_hardware_evidence_request_verify -ne 'ok') {
  $blockers.Add('hardware_evidence_request_not_ok')
}
if ($fieldPacket.pl_estop_field_packet_verify -ne 'ok') {
  $blockers.Add('field_packet_not_ok')
}
if ($fieldRunbook.pl_estop_field_runbook_verify -ne 'ok' -or
    $fieldRunbook.field_runbook_state -ne 'open') {
  $blockers.Add('field_runbook_not_ok')
}
if ($evidenceTemplates.pl_estop_evidence_templates_verify -ne 'ok' -or
    $evidenceTemplates.board_verified_template_records -ne '0') {
  $blockers.Add('evidence_templates_not_ok')
}
if ($evidenceRoot.pl_estop_evidence_root_verify -ne 'ok' -or
    $evidenceRoot.orphan_board_verified_records -ne '0') {
  $blockers.Add('evidence_root_not_ok')
}
if ($safety.pl_estop_safety_boundary -ne 'ok' -or
    $safety.active_pending_wiring_pin_assignments -ne '0' -or
    $safety.active_estop_gate_output_ports -ne '0') {
  $blockers.Add('safety_boundary_not_ok')
}
if ($wiring.ready_for_real_pins -ne 'yes') {
  $blockers.Add('wiring_not_ready_for_real_pins')
}
if ($wiring.board_evidence_ready -ne 'yes') {
  $blockers.Add('wiring_board_evidence_not_ready')
}
if ($boardValidation.board_validation_ready -ne 'yes') {
  $blockers.Add('board_validation_not_ready')
}
if ($wiring.board_verified_evidence_contract -ne 'md_non_placeholder' -or
    $wiring.board_verified_attachment_contract -ne 'project_relative_existing_files' -or
    $boardValidation.board_verified_evidence_contract -ne 'md_non_placeholder' -or
    $boardValidation.board_verified_attachment_contract -ne 'project_relative_existing_files') {
  $blockers.Add('board_verified_contract_not_ok')
}

$structuralContractOk = (
  $hardwareRequest.pl_estop_hardware_evidence_request_verify -eq 'ok' -and
  $fieldPacket.pl_estop_field_packet_verify -eq 'ok' -and
  $fieldRunbook.pl_estop_field_runbook_verify -eq 'ok' -and
  $fieldRunbook.field_runbook_state -eq 'open' -and
  $evidenceTemplates.pl_estop_evidence_templates_verify -eq 'ok' -and
  $evidenceTemplates.board_verified_template_records -eq '0' -and
  $evidenceRoot.pl_estop_evidence_root_verify -eq 'ok' -and
  $evidenceRoot.orphan_board_verified_records -eq '0' -and
  $safety.pl_estop_safety_boundary -eq 'ok' -and
  $safety.active_pending_wiring_pin_assignments -eq '0' -and
  $safety.active_estop_gate_output_ports -eq '0' -and
  $wiring.board_verified_evidence_contract -eq 'md_non_placeholder' -and
  $wiring.board_verified_attachment_contract -eq 'project_relative_existing_files' -and
  $boardValidation.board_verified_evidence_contract -eq 'md_non_placeholder' -and
  $boardValidation.board_verified_attachment_contract -eq 'project_relative_existing_files'
)

$readyForRealPinReview = ($structuralContractOk -and $wiring.ready_for_real_pins -eq 'yes')
$readyForBoardSlice = ($readyForRealPinReview -and $wiring.board_evidence_ready -eq 'yes' -and $boardValidation.board_validation_ready -eq 'yes')

$state = 'not_ready'
if ($readyForBoardSlice) {
  $state = 'ready_for_board_slice'
} elseif ($readyForRealPinReview) {
  $state = 'ready_for_real_pin_review'
}

Write-Output "pl_estop_field_intake=$state"
Write-Output "field_intake_structural_contract=$(if ($structuralContractOk) { 'ok' } else { 'not_ok' })"
Write-Output "field_intake_blockers=$($blockers -join ',')"
Write-Output "hardware_evidence_request_verify=$($hardwareRequest.pl_estop_hardware_evidence_request_verify)"
Write-Output "field_packet_verify=$($fieldPacket.pl_estop_field_packet_verify)"
Write-Output "field_runbook_verify=$($fieldRunbook.pl_estop_field_runbook_verify)"
Write-Output "field_runbook_state=$($fieldRunbook.field_runbook_state)"
Write-Output "evidence_templates_verify=$($evidenceTemplates.pl_estop_evidence_templates_verify)"
Write-Output "board_verified_template_records=$($evidenceTemplates.board_verified_template_records)"
Write-Output "evidence_root_verify=$($evidenceRoot.pl_estop_evidence_root_verify)"
Write-Output "orphan_board_verified_records=$($evidenceRoot.orphan_board_verified_records)"
Write-Output "pl_estop_wiring_evidence=$($wiring.pl_estop_wiring_evidence)"
Write-Output "ready_for_real_pins=$($wiring.ready_for_real_pins)"
Write-Output "board_evidence_ready=$($wiring.board_evidence_ready)"
Write-Output "do_pwm_ready_rows=$($wiring.do_pwm_ready_rows)"
Write-Output "bus_tx_ready_rows=$($wiring.bus_tx_ready_rows)"
Write-Output "verified_wiring_evidence_files=$($wiring.verified_wiring_evidence_files)"
Write-Output "pl_estop_board_validation=$($boardValidation.pl_estop_board_validation)"
Write-Output "board_validation_ready=$($boardValidation.board_validation_ready)"
Write-Output "verified_board_validation_evidence_files=$($boardValidation.verified_evidence_files)"
Write-Output 'board_verified_evidence_contract=md_non_placeholder'
Write-Output 'board_verified_attachment_contract=project_relative_existing_files'
Write-Output "pl_estop_safety_boundary=$($safety.pl_estop_safety_boundary)"
Write-Output "active_pending_wiring_pin_assignments=$($safety.active_pending_wiring_pin_assignments)"
Write-Output "active_estop_gate_output_ports=$($safety.active_estop_gate_output_ports)"
