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
    [string]$ScriptRelativePath,
    [string[]]$ExtraArgs = @()
  )

  $scriptPath = Join-Path $RootDir ($ScriptRelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
  if (-not (Test-Path -LiteralPath $scriptPath)) {
    throw "Missing script: $ScriptRelativePath"
  }
  $output = @(& powershell -NoProfile -ExecutionPolicy Bypass -File $scriptPath -ProjectDir $RootDir @ExtraArgs)
  if ($LASTEXITCODE -ne 0) {
    throw "$ScriptRelativePath failed: $($output -join '; ')"
  }
  return [ordered]@{
    script = $ScriptRelativePath
    output = $output
    parsed = Convert-KeyValueOutput -Lines $output
  }
}

$checks = [ordered]@{
  active_xdc = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/check_active_xdc.ps1'
  active_xdc_traceability = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_active_xdc_traceability.ps1'
  active_xdc_electrical_contract = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_active_xdc_electrical_contract.ps1'
  vivado_xsa_cleanliness = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_vivado_xsa_cleanliness.ps1'
  vivado_warning_summary = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_vivado_warning_summary.ps1'
  project_independence = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_project_independence.ps1'
  project_portability = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_project_portability.ps1'
  adc_mapping = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_adc_spi_mapping.ps1'
  legacy_axis_adc_boundary = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_no_legacy_axis_adc_boundary.ps1'
  z20_v15_io_owner_sim = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_z20_v15_io_owner_sim.ps1'
  remaining_drc = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_remaining_drc_ports.ps1'
  active_pin_conflicts = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/export_active_pin_conflicts.ps1'
  pl_estop_sim = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_sim.ps1'
  pl_estop_timing_params = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_timing_params.ps1'
  pl_estop_safety_boundary = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_safety_boundary.ps1'
  pl_estop_output_shutdown_contract = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_output_shutdown_contract.ps1'
  pl_estop_bus_gate_owner = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_bus_gate_owner.ps1'
  pl_estop_real_pin_promotion_gate = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_real_pin_promotion_gate.ps1'
  pl_estop_readiness = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_readiness.ps1'
  board_input_handoff = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_board_input_handoff.ps1' -ExtraArgs @('-StaticOnly')
  board_input_manifest = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_board_input_manifest.ps1'
}

$active = $checks.active_xdc.parsed
$traceability = $checks.active_xdc_traceability.parsed
$electrical = $checks.active_xdc_electrical_contract.parsed
$vivadoCleanliness = $checks.vivado_xsa_cleanliness.parsed
$vivadoWarnings = $checks.vivado_warning_summary.parsed
$independence = $checks.project_independence.parsed
$portability = $checks.project_portability.parsed
$adc = $checks.adc_mapping.parsed
$legacyBoundary = $checks.legacy_axis_adc_boundary.parsed
$ioOwnerSim = $checks.z20_v15_io_owner_sim.parsed
$remaining = $checks.remaining_drc.parsed
$conflicts = $checks.active_pin_conflicts.parsed
$sim = $checks.pl_estop_sim.parsed
$timingParams = $checks.pl_estop_timing_params.parsed
$safety = $checks.pl_estop_safety_boundary.parsed
$outputShutdown = $checks.pl_estop_output_shutdown_contract.parsed
$busGateOwner = $checks.pl_estop_bus_gate_owner.parsed
$realPinPromotion = $checks.pl_estop_real_pin_promotion_gate.parsed
$readiness = $checks.pl_estop_readiness.parsed
$handoff = $checks.board_input_handoff.parsed
$manifest = $checks.board_input_manifest.parsed

if ($active.unassigned_top_ports_count -ne '0' -or $active.missing -ne '[]' -or $active.duplicate_ports -ne '{}' -or $active.duplicate_pins -ne '{}') {
  throw 'Active XDC closure check failed'
}
if ($traceability.active_xdc_traceability -ne 'ok' -or
    $traceability.active_pin_assignments -ne '180' -or
    $traceability.traced_assignments -ne '180' -or
    $traceability.active_xdc_source -ne '../z20-v1_5_20260623.xdc' -or
    $traceability.old_project_xdc_dependency -ne 'none') {
  throw 'Active XDC traceability check failed'
}
if ($electrical.active_xdc_electrical_contract -ne 'ok' -or
    $electrical.active_pin_assignments -ne '180' -or
    $electrical.iostandard_assignments -ne '180' -or
    $electrical.lvcmos33_assignments -ne '180' -or
    $electrical.missing_iostandard_assignments -ne '0' -or
    $electrical.orphan_iostandard_assignments -ne '0' -or
    $electrical.non_lvcmos33_assignments -ne '0' -or
    $electrical.duplicate_iostandard_ports -ne '0' -or
    $electrical.malformed_iostandard_lines -ne '0') {
  throw 'Active XDC electrical contract check failed'
}
if ($vivadoCleanliness.vivado_xsa_cleanliness -ne 'ok' -or
    $vivadoCleanliness.active_constraints_loaded -ne 'mapped_only' -or
    $vivadoCleanliness.truth_source_xdc_loaded -ne 'no' -or
    $vivadoCleanliness.old_project_xdc_loaded -ne 'no' -or
    $vivadoCleanliness.truth_source_xdc_parse_count -ne '0' -or
    $vivadoCleanliness.old_project_xdc_parse_count -ne '0' -or
    $vivadoCleanliness.vivado_critical_warnings -ne '0' -or
    $vivadoCleanliness.vivado_errors -ne '0' -or
    $vivadoCleanliness.drc_blocking_rules -ne '0' -or
    $vivadoCleanliness.build_status -ne 'bitstream_generated' -or
    $vivadoCleanliness.timing_status -ne 'timing_met' -or
    $vivadoCleanliness.timing_margin_policy -ne 'timing_met_required_wns_0p100_advisory' -or
    $vivadoCleanliness.bitstream_artifact -ne 'current' -or
    $vivadoCleanliness.xsa_artifact -ne 'current') {
  throw 'Vivado/XSA cleanliness check failed'
}
if ($vivadoCleanliness.drc_warning_rules -eq '1' -and
    $vivadoCleanliness.drc_allowed_warning_rules -ne 'RTSTAT-10') {
  throw "Vivado/XSA cleanliness has unexpected DRC warning rules: $($vivadoCleanliness.drc_allowed_warning_rules)"
}
if ($vivadoCleanliness.drc_warning_rules -ne '0' -and $vivadoCleanliness.drc_warning_rules -ne '1') {
  throw "Vivado/XSA cleanliness reports unexpected DRC warning count: $($vivadoCleanliness.drc_warning_rules)"
}
if ($vivadoWarnings.vivado_warning_summary_verify -ne 'ok' -or
    $vivadoWarnings.unexpected_warning_codes -ne '0' -or
    $vivadoWarnings.constraint_truth_warning_lines -ne '0' -or
    $vivadoWarnings.warning_summary_boundary -ne 'known_noncritical_warning_classes') {
  throw 'Vivado warning summary check failed'
}
if ($vivadoWarnings.vivado_warning_lines -ne $vivadoCleanliness.vivado_warning_lines) {
  throw 'Vivado warning summary disagrees with Vivado/XSA cleanliness warning count'
}
if ($independence.project_independence -ne 'ok' -or $independence.project_path_relative -ne 'ok') {
  throw 'Project independence check failed'
}
if ($portability.project_portability -ne 'ok' -or $portability.absolute_path_scan -ne 'ok' -or $portability.old_project_dependency -ne 'ok' -or $portability.tmp_files -ne '0') {
  throw 'Project portability check failed'
}
if ($adc.adc_mapping -ne 'ok' -or
    $adc.adc_owner -ne 'XADC_VP_VN_ONE_CHANNEL' -or
    $adc.adc_xadc_pins -ne 'L11,M12' -or
    $adc.adc_spi_mapping -ne 'retired' -or
    $adc.adc_spi_pins -ne 'none' -or
    $adc.xadc_one_channel_adc -ne 'enabled' -or
    $adc.active_adc_spi_assignments -ne '0' -or
    $adc.active_xadc_assignments -ne '0') {
  throw 'ADC one-channel XADC mapping check failed'
}
if ($legacyBoundary.legacy_axis_adc_boundary -ne 'retired' -or
    $legacyBoundary.wrapper_axis_boundary -ne 'current_8bit' -or
    $legacyBoundary.bd_adc_spi_external_boundary -ne 'retired' -or
    $legacyBoundary.axis_functional_completion -ne 'vivado_io_owner_connected' -or
    $legacyBoundary.axis_motion_owner -ne 'step_ip_8axis_stepdir_encoder_direct' -or
    $legacyBoundary.axis_ena_owner -ne 'z20_v15_io_owner_axi_lite' -or
    $legacyBoundary.axis_78_encoder_processing -ne 'connected_to_step_ip' -or
    $legacyBoundary.di_mpg_alarm_processing -ne 'z20_v15_io_owner_input_registers' -or
    $legacyBoundary.do_pwm_normal_owner -ne 'z20_v15_io_owner_do_pwm' -or
    $legacyBoundary.rs485_boundary -ne 'exported_ps_uart1_emio' -or
    $legacyBoundary.touch_int_rst_boundary -ne 'z20_v15_io_owner_tp_int_rst') {
  throw 'Legacy axis/ADC boundary retirement or functional-gap contract check failed'
}
if ($ioOwnerSim.z20_v15_io_owner_sim -ne 'ok' -or
    $ioOwnerSim.z20_v15_io_owner_axi_lite_tb -ne 'pass' -or
    $ioOwnerSim.sim_outputs -ne 'persistent_none') {
  throw 'Z20 v1.5 IO owner simulation gate failed'
}
if ($remaining.csv_rows -ne '0' -or $remaining.csv_matches_check_active -ne 'yes' -or $remaining.active_pin_conflicts -ne '0') {
  throw 'Remaining DRC closure check failed'
}
if ($conflicts.active_pin_conflicts -ne '0') {
  throw 'Active pin conflict export is not closed'
}
if ($sim.pl_estop_sim -ne 'ok' -or
    $sim.pl_estop_core_tb -ne 'pass' -or
    $sim.pl_estop_axi_lite_tb -ne 'pass' -or
    $sim.sim_outputs -ne 'persistent_none') {
  throw 'PL E-stop simulation gate failed'
}
if ($timingParams.pl_estop_timing_params -ne 'ok' -or
    $timingParams.clock_hz -ne '100000000' -or
    $timingParams.debounce_ms -ne '10' -or
    $timingParams.debounce_cycles -ne '1000000' -or
    $timingParams.brake_lead_us -ne '50' -or
    $timingParams.brake_cycles -ne '5000' -or
    $timingParams.axis_count -ne '8' -or
    $timingParams.z_axis_index -ne '2' -or
    $timingParams.bd_axi_freq_hz -ne '100000000' -or
    $timingParams.bd_clock_net -ne 'processing_system7_0_FCLK_CLK0') {
  throw 'PL E-stop timing parameter gate failed'
}
if ($safety.pl_estop_safety_boundary -ne 'ok' -or
    $safety.do_pwm_gate -ne 'top_hard_gate_local_unverified' -or
    $safety.bus_tx_gate -ne 'top_rgmii_tx_gate_local_unverified' -or
    $safety.active_do_pwm_pin_assignments -ne '16' -or
    $safety.active_estop_input_pin_assignments -ne '1' -or
    $safety.active_pending_wiring_pin_assignments -ne '0' -or
    $safety.active_estop_gate_output_ports -ne '0') {
  throw 'PL E-stop safety boundary check failed'
}
if ($outputShutdown.pl_estop_output_shutdown_contract -ne 'code_review_only' -or
    $outputShutdown.do_pwm_contract -ne 'ok' -or
    $outputShutdown.bus_tx_contract -ne 'ok' -or
    $outputShutdown.bus_gate_owner -ne 'ps_gem1_emio_rgmii_local_verified' -or
    $outputShutdown.bus_gate_transport -ne 'EtherCAT over PS GEM1/EMIO' -or
    $outputShutdown.bus_gate_before_gmii2rgmii -ne 'ok' -or
    $outputShutdown.bus_gate_board_evidence -ne 'pending' -or
    $outputShutdown.do_pwm_wiring_rows -ne '16' -or
    $outputShutdown.do_pwm_pending_rows -ne '0' -or
    $outputShutdown.bus_tx_wiring_rows -ne '2' -or
    $outputShutdown.bus_tx_pending_rows -ne '0' -or
    $outputShutdown.active_do_pwm_pin_assignments -ne '16' -or
    $outputShutdown.active_bus_tx_pin_assignments -ne '0' -or
    $outputShutdown.active_output_gate_ports -ne '0') {
  throw 'PL E-stop output shutdown contract check failed'
}
if ($busGateOwner.pl_estop_bus_gate_owner -ne 'ps_gem1_emio_rgmii_local_verified' -or
    $busGateOwner.production_profile -ne 'ethercat' -or
    $busGateOwner.production_transport -ne 'EtherCAT over PS GEM1/EMIO' -or
    $busGateOwner.bd_enet1_emio -ne 'enabled' -or
    $busGateOwner.bd_ps_gem1_to_gmii2rgmii -ne 'ok' -or
    $busGateOwner.gate_inserted_before_gmii2rgmii -ne 'ok' -or
    $busGateOwner.tx_en_gated -ne 'yes' -or
    $busGateOwner.txd_forced_idle_zero -ne 'yes' -or
    $busGateOwner.tx_er_forced_idle_zero -ne 'yes' -or
    $busGateOwner.rx_path_preserved_by_design -ne 'yes' -or
    $busGateOwner.mdio_path_preserved_by_design -ne 'yes' -or
    $busGateOwner.board_evidence_state -ne 'pending') {
  throw 'PL E-stop bus gate owner check failed'
}
if ($realPinPromotion.pl_estop_real_pin_promotion_gate -ne 'local_hard_gate_promoted' -or
    $realPinPromotion.active_promoted_wiring_assignments -ne '17' -or
    $realPinPromotion.active_promoted_do_pwm_assignments -ne '16' -or
    $realPinPromotion.active_promoted_estop_input_assignments -ne '1' -or
    $realPinPromotion.active_promoted_bus_tx_assignments -ne '0' -or
    $realPinPromotion.active_estop_gate_output_ports -ne '0' -or
    $realPinPromotion.local_hard_gate_promoted -ne 'yes' -or
    $realPinPromotion.promotion_requires_e11 -ne 'no') {
  throw 'PL E-stop real-pin promotion gate check failed'
}
if ($readiness.pl_estop_field_intake -ne 'not_ready' -or
    $readiness.field_intake_structural_contract -ne 'ok' -or
    $readiness.pl_estop_field_runbook_verify -ne 'ok' -or
    $readiness.field_runbook_state -ne 'open' -or
    $readiness.board_verified_template_records -ne '0') {
  throw 'PL E-stop field-intake local contract check failed'
}
if ($readiness.pl_estop_readiness -ne 'not_ready' -or
    $readiness.e11_rtl_xdc_ready -ne 'no' -or
    $readiness.a11_board_validation_ready -ne 'no') {
  throw 'PL E-stop readiness is not in the expected local-only not-ready state'
}
if ($handoff.board_input_handoff_verify -ne 'ok' -or
    $handoff.board_input_handoff_state -ne 'local_verified_only' -or
    $handoff.board_input_handoff_active_xdc_electrical_contract -ne 'ok') {
  throw 'Board-input handoff check failed'
}
if ($manifest.board_closure_state -ne 'local_verified_only' -or
    $manifest.hashes -ne 'ok' -or
    $manifest.project_portability -ne 'ok' -or
    $manifest.adc_mapping -ne 'ok' -or
    $manifest.pl_estop_register_map -ne 'ok' -or
    $manifest.pl_estop_readiness -ne 'not_ready') {
  throw 'Board-input manifest check failed'
}

Write-Output 'new_vivado_local_closure=local_verified_only'
Write-Output "top_module=$($active.top_module)"
Write-Output "unassigned_top_ports_count=$($active.unassigned_top_ports_count)"
Write-Output "active_xdc_traceability=$($traceability.active_xdc_traceability)"
Write-Output "active_xdc_electrical_contract=$($electrical.active_xdc_electrical_contract)"
Write-Output "vivado_xsa_cleanliness=$($vivadoCleanliness.vivado_xsa_cleanliness)"
Write-Output "active_constraints_loaded=$($vivadoCleanliness.active_constraints_loaded)"
Write-Output "truth_source_xdc_loaded=$($vivadoCleanliness.truth_source_xdc_loaded)"
Write-Output "drc_blocking_rules=$($vivadoCleanliness.drc_blocking_rules)"
Write-Output "drc_allowed_warning_rules=$($vivadoCleanliness.drc_allowed_warning_rules)"
Write-Output "timing_wns=$($vivadoCleanliness.timing_wns)"
Write-Output "timing_margin_policy=$($vivadoCleanliness.timing_margin_policy)"
Write-Output "timing_margin_target_wns=$($vivadoCleanliness.timing_margin_target_wns)"
Write-Output "timing_margin_target_status=$($vivadoCleanliness.timing_margin_target_status)"
Write-Output "vivado_warning_summary_verify=$($vivadoWarnings.vivado_warning_summary_verify)"
Write-Output "vivado_warning_lines=$($vivadoWarnings.vivado_warning_lines)"
Write-Output "vivado_warning_codes=$($vivadoWarnings.vivado_warning_codes)"
Write-Output "constraint_truth_warning_lines=$($vivadoWarnings.constraint_truth_warning_lines)"
Write-Output "retired_hdmi_warning_lines=$($vivadoWarnings.retired_hdmi_warning_lines)"
Write-Output "project_independence=$($independence.project_independence)"
Write-Output "project_portability=$($portability.project_portability)"
Write-Output "absolute_path_scan=$($portability.absolute_path_scan)"
Write-Output "adc_mapping=$($adc.adc_mapping)"
Write-Output "adc_owner=$($adc.adc_owner)"
Write-Output "adc_xadc_pins=$($adc.adc_xadc_pins)"
Write-Output "adc_spi_mapping=$($adc.adc_spi_mapping)"
Write-Output "legacy_axis_adc_boundary=$($legacyBoundary.legacy_axis_adc_boundary)"
Write-Output "wrapper_axis_boundary=$($legacyBoundary.wrapper_axis_boundary)"
Write-Output "axis_functional_completion=$($legacyBoundary.axis_functional_completion)"
Write-Output "axis_motion_owner=$($legacyBoundary.axis_motion_owner)"
Write-Output "axis_ena_owner=$($legacyBoundary.axis_ena_owner)"
Write-Output "axis_78_encoder_processing=$($legacyBoundary.axis_78_encoder_processing)"
Write-Output "di_mpg_alarm_processing=$($legacyBoundary.di_mpg_alarm_processing)"
Write-Output "do_pwm_normal_owner=$($legacyBoundary.do_pwm_normal_owner)"
Write-Output "rs485_boundary=$($legacyBoundary.rs485_boundary)"
Write-Output "touch_int_rst_boundary=$($legacyBoundary.touch_int_rst_boundary)"
Write-Output "z20_v15_io_owner_sim=$($ioOwnerSim.z20_v15_io_owner_sim)"
Write-Output "remaining_drc_rows=$($remaining.csv_rows)"
Write-Output "active_pin_conflicts=$($conflicts.active_pin_conflicts)"
Write-Output "pl_estop_sim=$($sim.pl_estop_sim)"
Write-Output "pl_estop_timing_params=$($timingParams.pl_estop_timing_params)"
Write-Output "pl_estop_safety_boundary=$($safety.pl_estop_safety_boundary)"
Write-Output "pl_estop_output_shutdown_contract=$($outputShutdown.pl_estop_output_shutdown_contract)"
Write-Output "pl_estop_bus_gate_owner=$($busGateOwner.pl_estop_bus_gate_owner)"
Write-Output "pl_estop_register_map=$($manifest.pl_estop_register_map)"
Write-Output "bus_gate_transport=$($busGateOwner.production_transport)"
Write-Output "bus_gate_board_evidence=$($busGateOwner.board_evidence_state)"
Write-Output "pl_estop_real_pin_promotion_gate=$($realPinPromotion.pl_estop_real_pin_promotion_gate)"
Write-Output "pl_estop_field_intake=$($readiness.pl_estop_field_intake)"
Write-Output "pl_estop_readiness=$($readiness.pl_estop_readiness)"
Write-Output "board_input_handoff_verify=$($handoff.board_input_handoff_verify)"
Write-Output "board_input_manifest_hashes=$($manifest.hashes)"
Write-Output "board_closure_state=$($manifest.board_closure_state)"
