param(
  [string]$ProjectDir,
  [string]$ManifestPath
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
  $ManifestPath = Join-Path $ProjectDir 'board_inputs/manifest.json'
}

function Test-RelativePath {
  param([string]$Path)
  if ([string]::IsNullOrWhiteSpace($Path)) {
    return $false
  }
  if ([System.IO.Path]::IsPathRooted($Path)) {
    return $false
  }
  if ($Path -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
    return $false
  }
  if (($Path -split '[\\/]') -contains '..') {
    return $false
  }
  return $true
}

function Get-ProjectFile {
  param(
    [string]$RootDir,
    [string]$RelativePath
  )
  if (-not (Test-RelativePath -Path $RelativePath)) {
    throw "Manifest path is not project-relative: $RelativePath"
  }
  $fullPath = Join-Path $RootDir ($RelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
  if (-not (Test-Path -LiteralPath $fullPath)) {
    throw "Manifest artifact is missing: $RelativePath"
  }
  return Get-Item -LiteralPath $fullPath
}

if (-not (Test-Path -LiteralPath $ManifestPath)) {
  throw 'Missing board_inputs/manifest.json'
}

$manifestText = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8
if ($manifestText -match '(?<![A-Za-z])[A-Za-z]:[\\/]' -or $manifestText -match 'vivado_hw_project') {
  throw 'Manifest contains an absolute path or old-project reference'
}

$manifest = $manifestText | ConvertFrom-Json
if ($manifest.schema -ne 'z20_v1_5_board_input_manifest.v1') {
  throw "Unexpected manifest schema: $($manifest.schema)"
}
if ($manifest.board_closure_state -ne 'local_verified_only') {
  throw "Unexpected board_closure_state: $($manifest.board_closure_state)"
}
if ($manifest.artifact_paths_are_relative_to -ne 'new-vivado/z20_v1_5_hw_project') {
  throw "Unexpected artifact path root: $($manifest.artifact_paths_are_relative_to)"
}
if ($manifest.timing_latest.build_status -ne 'bitstream_generated') {
  throw "Manifest build_status is not bitstream_generated: $($manifest.timing_latest.build_status)"
}
if ($manifest.timing_latest.timing_status -ne 'timing_met') {
  throw "Manifest timing_status is not timing_met: $($manifest.timing_latest.timing_status)"
}
$manifestTimingWns = [double]::Parse([string]$manifest.timing_latest.wns, [System.Globalization.CultureInfo]::InvariantCulture)
if ($manifestTimingWns -lt 0.000) {
  throw "Manifest WNS is negative: $($manifest.timing_latest.wns)"
}

$parsedActive = $manifest.checks.check_active_xdc.parsed
$parsedTraceability = $manifest.checks.active_xdc_traceability.parsed
$parsedElectrical = $manifest.checks.active_xdc_electrical_contract.parsed
$parsedVivadoCleanliness = $manifest.checks.vivado_xsa_cleanliness.parsed
$parsedHardwareAbi = $manifest.checks.hardware_abi_header.parsed
$parsedHardwareAbiVerify = $manifest.checks.hardware_abi_header_verify.parsed
$parsedDriverContract = $manifest.checks.driver_contract.parsed
$parsedDriverContractVerify = $manifest.checks.driver_contract_verify.parsed
$parsedVivadoWarningSummary = $manifest.checks.vivado_warning_summary.parsed
$parsedVivadoWarningSummaryVerify = $manifest.checks.vivado_warning_summary_verify.parsed
$parsedIndependence = $manifest.checks.project_independence.parsed
$parsedPortability = $manifest.checks.project_portability.parsed
$parsedAdc = $manifest.checks.adc_mapping.parsed
$parsedLegacyBoundary = $manifest.checks.legacy_axis_adc_boundary.parsed
$parsedIoOwnerSim = $manifest.checks.z20_v15_io_owner_sim.parsed
$parsedSim = $manifest.checks.pl_estop_sim.parsed
$parsedTimingParams = $manifest.checks.pl_estop_timing_params.parsed
$parsedEstopBoundary = $manifest.checks.pl_estop_safety_boundary.parsed
$parsedOutputShutdown = $manifest.checks.pl_estop_output_shutdown_contract.parsed
$parsedBusGateOwner = $manifest.checks.pl_estop_bus_gate_owner.parsed
$parsedRegisterMap = $manifest.checks.pl_estop_register_map.parsed
$parsedWiringEvidence = $manifest.checks.pl_estop_wiring_evidence.parsed
$parsedBoardValidation = $manifest.checks.pl_estop_board_validation.parsed
$parsedEvidenceGap = $manifest.checks.pl_estop_evidence_gap.parsed
$parsedHardwareEvidenceRequest = $manifest.checks.pl_estop_hardware_evidence_request.parsed
$parsedHardwareEvidenceRequestVerify = $manifest.checks.pl_estop_hardware_evidence_request_verify.parsed
$parsedFieldPacket = $manifest.checks.pl_estop_field_packet.parsed
$parsedFieldPacketVerify = $manifest.checks.pl_estop_field_packet_verify.parsed
$parsedFieldRunbook = $manifest.checks.pl_estop_field_runbook.parsed
$parsedEvidenceTemplates = $manifest.checks.pl_estop_evidence_templates.parsed
$parsedEvidenceTemplatesVerify = $manifest.checks.pl_estop_evidence_templates_verify.parsed
$parsedEvidenceRoot = $manifest.checks.pl_estop_evidence_root.parsed
$parsedFieldIntake = $manifest.checks.pl_estop_field_intake.parsed
$parsedRealPinPromotion = $manifest.checks.pl_estop_real_pin_promotion_gate.parsed
$parsedReadiness = $manifest.checks.pl_estop_readiness.parsed
$parsedBoardInputHandoff = $manifest.checks.board_input_handoff.parsed
$parsedBoardInputHandoffVerify = $manifest.checks.board_input_handoff_verify.parsed
$parsedRemaining = $manifest.checks.verify_remaining_drc_ports.parsed
$parsedConflicts = $manifest.checks.export_active_pin_conflicts.parsed

if ($parsedActive.unassigned_top_ports_count -ne '0') {
  throw "Manifest unassigned_top_ports_count is not zero: $($parsedActive.unassigned_top_ports_count)"
}
if ($parsedTraceability.active_xdc_traceability -ne 'ok' -or
    $parsedTraceability.active_pin_assignments -ne '180' -or
    $parsedTraceability.traced_assignments -ne '180' -or
    $parsedTraceability.active_xdc_source -ne '../z20-v1_5_20260623.xdc' -or
    $parsedTraceability.old_project_xdc_dependency -ne 'none') {
  throw 'Manifest active XDC traceability check is not closed'
}
if ($parsedElectrical.active_xdc_electrical_contract -ne 'ok' -or
    $parsedElectrical.active_pin_assignments -ne '180' -or
    $parsedElectrical.iostandard_assignments -ne '180' -or
    $parsedElectrical.lvcmos33_assignments -ne '180' -or
    $parsedElectrical.missing_iostandard_assignments -ne '0' -or
    $parsedElectrical.orphan_iostandard_assignments -ne '0' -or
    $parsedElectrical.non_lvcmos33_assignments -ne '0' -or
    $parsedElectrical.duplicate_iostandard_ports -ne '0' -or
    $parsedElectrical.malformed_iostandard_lines -ne '0') {
  throw 'Manifest active XDC electrical contract check is not closed'
}
if ($parsedVivadoCleanliness.vivado_xsa_cleanliness -ne 'ok' -or
    $parsedVivadoCleanliness.active_constraints_loaded -ne 'mapped_only' -or
    $parsedVivadoCleanliness.truth_source_xdc_loaded -ne 'no' -or
    $parsedVivadoCleanliness.old_project_xdc_loaded -ne 'no' -or
    $parsedVivadoCleanliness.truth_source_xdc_parse_count -ne '0' -or
    $parsedVivadoCleanliness.old_project_xdc_parse_count -ne '0' -or
    $parsedVivadoCleanliness.vivado_critical_warnings -ne '0' -or
    $parsedVivadoCleanliness.vivado_errors -ne '0' -or
    $parsedVivadoCleanliness.drc_blocking_rules -ne '0' -or
    $parsedVivadoCleanliness.build_status -ne 'bitstream_generated' -or
    $parsedVivadoCleanliness.timing_status -ne 'timing_met' -or
    $parsedVivadoCleanliness.timing_margin_policy -ne 'timing_met_required_wns_0p100_advisory' -or
    $parsedVivadoCleanliness.bitstream_artifact -ne 'current' -or
    $parsedVivadoCleanliness.xsa_artifact -ne 'current') {
  throw 'Manifest Vivado/XSA cleanliness check is not closed'
}
if ($parsedVivadoCleanliness.drc_warning_rules -eq '1' -and
    $parsedVivadoCleanliness.drc_allowed_warning_rules -ne 'RTSTAT-10') {
  throw "Manifest Vivado/XSA cleanliness has unexpected DRC warning rules: $($parsedVivadoCleanliness.drc_allowed_warning_rules)"
}
if ($parsedVivadoCleanliness.drc_warning_rules -ne '0' -and $parsedVivadoCleanliness.drc_warning_rules -ne '1') {
  throw "Manifest Vivado/XSA cleanliness reports unexpected DRC warning count: $($parsedVivadoCleanliness.drc_warning_rules)"
}
if ($parsedHardwareAbi.hardware_abi_header -ne 'ok' -or
    $parsedHardwareAbi.mode -ne 'write' -or
    $parsedHardwareAbi.output -ne 'board_inputs/v3_hardware_abi.h' -or
    $parsedHardwareAbi.xsa -ne 'board_inputs/system.xsa' -or
    $parsedHardwareAbi.blocks -ne '3' -or
    $parsedHardwareAbi.step_ip_base -ne '0x43CB0000' -or
    $parsedHardwareAbi.step_ip_range -ne '0x00010000' -or
    $parsedHardwareAbi.pl_estop_base -ne '0x41260000' -or
    $parsedHardwareAbi.pl_estop_range -ne '0x00010000' -or
    $parsedHardwareAbi.io_owner_base -ne '0x41270000' -or
    $parsedHardwareAbi.io_owner_range -ne '0x00010000') {
  throw 'Manifest hardware ABI header generation check is not closed'
}
if ($parsedHardwareAbiVerify.hardware_abi_header -ne 'ok' -or
    $parsedHardwareAbiVerify.mode -ne 'check' -or
    $parsedHardwareAbiVerify.output -ne 'board_inputs/v3_hardware_abi.h' -or
    $parsedHardwareAbiVerify.xsa -ne 'board_inputs/system.xsa' -or
    $parsedHardwareAbiVerify.blocks -ne '3' -or
    $parsedHardwareAbiVerify.step_ip_base -ne '0x43CB0000' -or
    $parsedHardwareAbiVerify.step_ip_range -ne '0x00010000' -or
    $parsedHardwareAbiVerify.pl_estop_base -ne '0x41260000' -or
    $parsedHardwareAbiVerify.pl_estop_range -ne '0x00010000' -or
    $parsedHardwareAbiVerify.io_owner_base -ne '0x41270000' -or
    $parsedHardwareAbiVerify.io_owner_range -ne '0x00010000') {
  throw 'Manifest hardware ABI header verify check is not closed'
}
if ($parsedDriverContract.driver_contract -ne 'ok' -or
    $parsedDriverContract.mode -ne 'write' -or
    $parsedDriverContract.header -ne 'board_inputs/z20_v1_5_hardware_abi.h' -or
    $parsedDriverContract.dtsi -ne 'board_inputs/z20_v1_5_axi_lite.dtsi' -or
    $parsedDriverContract.xsa -ne 'board_inputs/system.xsa' -or
    $parsedDriverContract.blocks -ne '3' -or
    $parsedDriverContract.pl_estop_irq_f2p_bit -ne '14' -or
    $parsedDriverContract.pl_estop_irq_gic_spi_cell -ne '43' -or
    $parsedDriverContract.step_ip_base -ne '0x43CB0000' -or
    $parsedDriverContract.step_ip_range -ne '0x00010000' -or
    $parsedDriverContract.pl_estop_base -ne '0x41260000' -or
    $parsedDriverContract.pl_estop_range -ne '0x00010000' -or
    $parsedDriverContract.io_owner_base -ne '0x41270000' -or
    $parsedDriverContract.io_owner_range -ne '0x00010000') {
  throw 'Manifest driver contract generation check is not closed'
}
if ($parsedDriverContractVerify.driver_contract -ne 'ok' -or
    $parsedDriverContractVerify.mode -ne 'check' -or
    $parsedDriverContractVerify.header -ne 'board_inputs/z20_v1_5_hardware_abi.h' -or
    $parsedDriverContractVerify.dtsi -ne 'board_inputs/z20_v1_5_axi_lite.dtsi' -or
    $parsedDriverContractVerify.xsa -ne 'board_inputs/system.xsa' -or
    $parsedDriverContractVerify.blocks -ne '3' -or
    $parsedDriverContractVerify.pl_estop_irq_f2p_bit -ne '14' -or
    $parsedDriverContractVerify.pl_estop_irq_gic_spi_cell -ne '43' -or
    $parsedDriverContractVerify.step_ip_base -ne '0x43CB0000' -or
    $parsedDriverContractVerify.step_ip_range -ne '0x00010000' -or
    $parsedDriverContractVerify.pl_estop_base -ne '0x41260000' -or
    $parsedDriverContractVerify.pl_estop_range -ne '0x00010000' -or
    $parsedDriverContractVerify.io_owner_base -ne '0x41270000' -or
    $parsedDriverContractVerify.io_owner_range -ne '0x00010000') {
  throw 'Manifest driver contract verify check is not closed'
}
if ($parsedVivadoWarningSummary.vivado_warning_summary -ne 'classified' -or
    $parsedVivadoWarningSummary.unexpected_warning_codes -ne '0' -or
    $parsedVivadoWarningSummary.constraint_truth_warning_lines -ne '0' -or
    $parsedVivadoWarningSummary.warning_summary_boundary -ne 'known_noncritical_warning_classes') {
  throw 'Manifest Vivado warning summary is not classified'
}
if ($parsedVivadoWarningSummaryVerify.vivado_warning_summary_verify -ne 'ok' -or
    $parsedVivadoWarningSummaryVerify.unexpected_warning_codes -ne '0' -or
    $parsedVivadoWarningSummaryVerify.constraint_truth_warning_lines -ne '0' -or
    $parsedVivadoWarningSummaryVerify.warning_summary_boundary -ne 'known_noncritical_warning_classes') {
  throw 'Manifest Vivado warning summary verifier is not closed'
}
if ($parsedVivadoWarningSummary.vivado_warning_lines -ne $parsedVivadoCleanliness.vivado_warning_lines -or
    $parsedVivadoWarningSummaryVerify.vivado_warning_lines -ne $parsedVivadoCleanliness.vivado_warning_lines) {
  throw 'Manifest Vivado warning line counts disagree'
}
if ($parsedIndependence.project_independence -ne 'ok' -or $parsedIndependence.project_path_relative -ne 'ok') {
  throw 'Manifest project independence check is not closed'
}
if ($parsedPortability.project_portability -ne 'ok' -or
    $parsedPortability.absolute_path_scan -ne 'ok' -or
    $parsedPortability.old_project_dependency -ne 'ok' -or
    $parsedPortability.manifest_relative_paths -ne 'ok' -or
    $parsedPortability.tmp_files -ne '0') {
  throw 'Manifest project portability check is not closed'
}
if ($parsedAdc.adc_mapping -ne 'ok' -or
    $parsedAdc.adc_owner -ne 'XADC_VP_VN_ONE_CHANNEL' -or
    $parsedAdc.adc_channel_count -ne '1' -or
    $parsedAdc.adc_xadc_pins -ne 'L11,M12' -or
    $parsedAdc.adc_spi_mapping -ne 'retired' -or
    $parsedAdc.adc_spi_pins -ne 'none' -or
    $parsedAdc.xadc_one_channel_adc -ne 'enabled' -or
    $parsedAdc.xadc_two_channel_adc -ne 'not_used' -or
    $parsedAdc.legacy_fpga1_io12_spare_constraints -ne 'source_restored_active_unassigned' -or
    $parsedAdc.active_adc_spi_assignments -ne '0' -or
    $parsedAdc.active_xadc_assignments -ne '0') {
  throw 'Manifest ADC one-channel XADC mapping check is not closed'
}
if ($parsedLegacyBoundary.legacy_axis_adc_boundary -ne 'retired' -or
    $parsedLegacyBoundary.wrapper_axis_boundary -ne 'current_8bit' -or
    $parsedLegacyBoundary.bd_adc_spi_external_boundary -ne 'retired' -or
    $parsedLegacyBoundary.axis_functional_completion -ne 'vivado_io_owner_connected' -or
    $parsedLegacyBoundary.axis_motion_owner -ne 'step_ip_8axis_stepdir_encoder_direct' -or
    $parsedLegacyBoundary.axis_ena_owner -ne 'z20_v15_io_owner_axi_lite' -or
    $parsedLegacyBoundary.axis_78_encoder_processing -ne 'connected_to_step_ip' -or
    $parsedLegacyBoundary.di_mpg_alarm_processing -ne 'z20_v15_io_owner_input_registers' -or
    $parsedLegacyBoundary.do_pwm_normal_owner -ne 'z20_v15_io_owner_do_pwm' -or
    $parsedLegacyBoundary.rs485_boundary -ne 'exported_ps_uart1_emio' -or
    $parsedLegacyBoundary.touch_int_rst_boundary -ne 'z20_v15_io_owner_tp_int_rst') {
  throw 'Manifest legacy axis/ADC boundary and functional-gap check is not closed'
}
if ($manifest.safety_boundaries.legacy_axis_adc_boundary -ne 'retired_external_boundary_only' -or
    $manifest.safety_boundaries.axis_functional_completion -ne 'vivado_io_owner_connected' -or
    $manifest.safety_boundaries.axis_ena_owner -ne 'z20_v15_io_owner_axi_lite' -or
    $manifest.safety_boundaries.axis_78_encoder_processing -ne 'connected_to_step_ip' -or
    $manifest.safety_boundaries.di_mpg_alarm_processing -ne 'z20_v15_io_owner_input_registers' -or
    $manifest.safety_boundaries.do_pwm_normal_owner -ne 'z20_v15_io_owner_do_pwm' -or
    $manifest.safety_boundaries.rs485_boundary -ne 'exported_ps_uart1_emio' -or
    $manifest.safety_boundaries.touch_int_rst_boundary -ne 'z20_v15_io_owner_tp_int_rst') {
  throw 'Manifest safety boundary functional-gap state is not explicit'
}
if ($parsedIoOwnerSim.z20_v15_io_owner_sim -ne 'ok' -or
    $parsedIoOwnerSim.z20_v15_io_owner_axi_lite_tb -ne 'pass' -or
    $parsedIoOwnerSim.sim_outputs -ne 'persistent_none') {
  throw 'Manifest Z20 v1.5 IO owner simulation gate is not closed'
}
if ($parsedSim.pl_estop_sim -ne 'ok' -or
    $parsedSim.pl_estop_core_tb -ne 'pass' -or
    $parsedSim.pl_estop_axi_lite_tb -ne 'pass' -or
    $parsedSim.sim_tool -ne 'icarus_verilog' -or
    $parsedSim.sim_outputs -ne 'persistent_none') {
  throw 'Manifest PL E-stop simulation gate is not closed'
}
if ($parsedTimingParams.pl_estop_timing_params -ne 'ok' -or
    $parsedTimingParams.clock_hz -ne '100000000' -or
    $parsedTimingParams.debounce_ms -ne '10' -or
    $parsedTimingParams.debounce_cycles -ne '1000000' -or
    $parsedTimingParams.brake_lead_us -ne '50' -or
    $parsedTimingParams.brake_cycles -ne '5000' -or
    $parsedTimingParams.axis_count -ne '8' -or
    $parsedTimingParams.z_axis_index -ne '2' -or
    $parsedTimingParams.bd_axi_freq_hz -ne '100000000' -or
    $parsedTimingParams.bd_clock_net -ne 'processing_system7_0_FCLK_CLK0' -or
    $parsedTimingParams.register_timing_exposed -ne 'yes' -or
    $parsedTimingParams.register_axis_config_exposed -ne 'yes') {
  throw 'Manifest PL E-stop timing parameter gate is not closed'
}
if ($parsedEstopBoundary.pl_estop_safety_boundary -ne 'ok' -or
    $parsedEstopBoundary.do_pwm_gate -ne 'top_hard_gate_local_unverified' -or
    $parsedEstopBoundary.bus_tx_gate -ne 'top_rgmii_tx_gate_local_unverified' -or
    $parsedEstopBoundary.active_do_pwm_pin_assignments -ne '16' -or
    $parsedEstopBoundary.active_estop_input_pin_assignments -ne '1' -or
    $parsedEstopBoundary.active_pending_wiring_pin_assignments -ne '0' -or
    $parsedEstopBoundary.active_estop_gate_output_ports -ne '0') {
  throw 'Manifest PL E-stop safety boundary check is not closed'
}
if ($parsedOutputShutdown.pl_estop_output_shutdown_contract -ne 'code_review_only' -or
    $parsedOutputShutdown.do_pwm_contract -ne 'ok' -or
    $parsedOutputShutdown.bus_tx_contract -ne 'ok' -or
    $parsedOutputShutdown.bus_gate_owner -ne 'ps_gem1_emio_rgmii_local_verified' -or
    $parsedOutputShutdown.bus_gate_transport -ne 'EtherCAT over PS GEM1/EMIO' -or
    $parsedOutputShutdown.bus_gate_before_gmii2rgmii -ne 'ok' -or
    $parsedOutputShutdown.bus_gate_board_evidence -ne 'pending' -or
    $parsedOutputShutdown.do_pwm_wiring_rows -ne '16' -or
    $parsedOutputShutdown.do_pwm_pending_rows -ne '0' -or
    $parsedOutputShutdown.bus_tx_wiring_rows -ne '2' -or
    $parsedOutputShutdown.bus_tx_pending_rows -ne '0' -or
    $parsedOutputShutdown.board_test_bv09 -ne 'pending' -or
    $parsedOutputShutdown.board_test_bv10 -ne 'pending' -or
    $parsedOutputShutdown.board_test_bv11 -ne 'pending' -or
    $parsedOutputShutdown.board_test_bv12 -ne 'pending' -or
    $parsedOutputShutdown.active_do_pwm_pin_assignments -ne '16' -or
    $parsedOutputShutdown.active_bus_tx_pin_assignments -ne '0' -or
    $parsedOutputShutdown.active_output_gate_ports -ne '0') {
  throw 'Manifest PL E-stop output shutdown contract is not in the expected local hard-gate, field-evidence-required state'
}
if ($parsedBusGateOwner.pl_estop_bus_gate_owner -ne 'ps_gem1_emio_rgmii_local_verified' -or
    $parsedBusGateOwner.production_profile -ne 'ethercat' -or
    $parsedBusGateOwner.production_transport -ne 'EtherCAT over PS GEM1/EMIO' -or
    $parsedBusGateOwner.bd_enet1_emio -ne 'enabled' -or
    $parsedBusGateOwner.bd_ps_gem1_to_gmii2rgmii -ne 'ok' -or
    $parsedBusGateOwner.gate_inserted_before_gmii2rgmii -ne 'ok' -or
    $parsedBusGateOwner.tx_en_gated -ne 'yes' -or
    $parsedBusGateOwner.txd_forced_idle_zero -ne 'yes' -or
    $parsedBusGateOwner.tx_er_forced_idle_zero -ne 'yes' -or
    $parsedBusGateOwner.rx_path_preserved_by_design -ne 'yes' -or
    $parsedBusGateOwner.mdio_path_preserved_by_design -ne 'yes' -or
    $parsedBusGateOwner.link_reset_power_gate -ne 'not_used' -or
    $parsedBusGateOwner.board_tests_required -ne 'BV10,BV11,BV12' -or
    $parsedBusGateOwner.board_evidence_state -ne 'pending') {
  throw 'Manifest PL E-stop bus gate owner check is not closed'
}
if ($parsedRegisterMap.pl_estop_register_map -ne 'ok' -or
    $parsedRegisterMap.register_map -ne 'board_inputs/pl_estop_register_map.md' -or
    $parsedRegisterMap.rtl_source -ne 'rtl/pl_estop_axi_lite.v' -or
    $parsedRegisterMap.registers -ne '9' -or
    $parsedRegisterMap.status_bits -ne '9' -or
    $parsedRegisterMap.control_bits -ne '2' -or
    $parsedRegisterMap.base_address -ne '0x41260000' -or
    $parsedRegisterMap.irq_route -ne 'xlconcat_0/In14' -or
    $parsedRegisterMap.abi_version -ne '0x00010001') {
  throw 'Manifest PL E-stop register-map check is not closed'
}
if ($parsedWiringEvidence.pl_estop_wiring_evidence -ne 'not_ready' -or
    $parsedWiringEvidence.ready_for_real_pins -ne 'no' -or
    $parsedWiringEvidence.board_evidence_ready -ne 'no' -or
    $parsedWiringEvidence.verified_wiring_evidence_files -ne '0' -or
    $parsedWiringEvidence.board_verified_evidence_contract -ne 'md_non_placeholder' -or
    $parsedWiringEvidence.board_verified_attachment_contract -ne 'project_relative_existing_files') {
  throw 'Manifest PL E-stop wiring evidence state is not the expected local-only not_ready state'
}
if ($parsedBoardValidation.pl_estop_board_validation -ne 'not_ready' -or
    $parsedBoardValidation.board_validation_ready -ne 'no' -or
    $parsedBoardValidation.verified_evidence_files -ne '0' -or
    $parsedBoardValidation.board_verified_evidence_contract -ne 'md_non_placeholder' -or
    $parsedBoardValidation.board_verified_attachment_contract -ne 'project_relative_existing_files') {
  throw 'Manifest PL E-stop board validation state is not the expected local-only not_ready state'
}
if ($parsedEvidenceGap.pl_estop_evidence_gap -ne 'open' -or
    $parsedEvidenceGap.wiring_pending_rows -ne '3' -or
    $parsedEvidenceGap.board_pending_tests -ne '14') {
  throw 'Manifest PL E-stop evidence gap state is not the expected open local-only state'
}
if ($parsedHardwareEvidenceRequest.pl_estop_hardware_evidence_request -ne 'open' -or
    $parsedHardwareEvidenceRequest.wiring_request_items -ne '3' -or
    $parsedHardwareEvidenceRequest.board_request_items -ne '14' -or
    $parsedHardwareEvidenceRequest.do_pwm_request_items -ne '0' -or
    $parsedHardwareEvidenceRequest.bus_tx_request_items -ne '0') {
  throw 'Manifest PL E-stop hardware evidence request state is not the expected open local-only state'
}
if ($parsedHardwareEvidenceRequestVerify.pl_estop_hardware_evidence_request_verify -ne 'ok' -or
    $parsedHardwareEvidenceRequestVerify.hardware_evidence_request_state -ne 'open' -or
    $parsedHardwareEvidenceRequestVerify.wiring_request_items -ne '3' -or
    $parsedHardwareEvidenceRequestVerify.board_request_items -ne '14' -or
    $parsedHardwareEvidenceRequestVerify.do_pwm_request_items -ne '0' -or
    $parsedHardwareEvidenceRequestVerify.bus_tx_request_items -ne '0') {
  throw 'Manifest PL E-stop hardware evidence request verify check is not closed'
}
if ($parsedFieldPacket.pl_estop_field_packet -ne 'open' -or
    $parsedFieldPacket.wiring_rows -ne '22' -or
    $parsedFieldPacket.board_tests -ne '14' -or
    $parsedFieldPacket.do_pwm_rows -ne '16' -or
    $parsedFieldPacket.bus_tx_rows -ne '2') {
  throw 'Manifest PL E-stop field packet state is not the expected open local-only state'
}
if ($parsedFieldPacketVerify.pl_estop_field_packet_verify -ne 'ok' -or
    $parsedFieldPacketVerify.field_packet_state -ne 'open' -or
    $parsedFieldPacketVerify.wiring_rows -ne '22' -or
    $parsedFieldPacketVerify.board_tests -ne '14' -or
    $parsedFieldPacketVerify.do_pwm_rows -ne '16' -or
    $parsedFieldPacketVerify.bus_tx_rows -ne '2') {
  throw 'Manifest PL E-stop field packet verify check is not closed'
}
if ($parsedFieldRunbook.pl_estop_field_runbook_verify -ne 'ok' -or
    $parsedFieldRunbook.field_runbook_state -ne 'open' -or
    $parsedFieldRunbook.field_runbook -ne 'docs/pl_estop_field_execution_runbook.md') {
  throw 'Manifest PL E-stop field execution runbook check is not closed'
}
if ($parsedEvidenceTemplates.pl_estop_evidence_templates -ne 'open' -or
    $parsedEvidenceTemplates.wiring_templates -ne '22' -or
    $parsedEvidenceTemplates.board_validation_templates -ne '14' -or
    $parsedEvidenceTemplates.do_pwm_templates -ne '16' -or
    $parsedEvidenceTemplates.bus_tx_templates -ne '2' -or
    $parsedEvidenceTemplates.board_verified_template_records -ne '0') {
  throw 'Manifest PL E-stop evidence templates state is not the expected open local-only state'
}
if ($parsedEvidenceTemplatesVerify.pl_estop_evidence_templates_verify -ne 'ok' -or
    $parsedEvidenceTemplatesVerify.template_state -ne 'open' -or
    $parsedEvidenceTemplatesVerify.wiring_templates -ne '22' -or
    $parsedEvidenceTemplatesVerify.board_validation_templates -ne '14' -or
    $parsedEvidenceTemplatesVerify.do_pwm_templates -ne '16' -or
    $parsedEvidenceTemplatesVerify.bus_tx_templates -ne '2' -or
    $parsedEvidenceTemplatesVerify.board_verified_template_records -ne '0') {
  throw 'Manifest PL E-stop evidence templates verify check is not closed'
}
if ($parsedEvidenceRoot.pl_estop_evidence_root_verify -ne 'ok' -or
    $parsedEvidenceRoot.evidence_root -ne 'docs/evidence/pl_estop' -or
    $parsedEvidenceRoot.evidence_root_files -ne '1' -or
    $parsedEvidenceRoot.evidence_root_md_files -ne '1' -or
    $parsedEvidenceRoot.referenced_board_verified_evidence_paths -ne '0' -or
    $parsedEvidenceRoot.board_verified_records -ne '0' -or
    $parsedEvidenceRoot.orphan_board_verified_records -ne '0') {
  throw 'Manifest PL E-stop evidence root verify check is not closed'
}
if ($parsedFieldIntake.pl_estop_field_intake -ne 'not_ready' -or
    $parsedFieldIntake.field_intake_structural_contract -ne 'ok' -or
    $parsedFieldIntake.hardware_evidence_request_verify -ne 'ok' -or
    $parsedFieldIntake.field_packet_verify -ne 'ok' -or
    $parsedFieldIntake.field_runbook_verify -ne 'ok' -or
    $parsedFieldIntake.field_runbook_state -ne 'open' -or
    $parsedFieldIntake.evidence_templates_verify -ne 'ok' -or
    $parsedFieldIntake.board_verified_template_records -ne '0' -or
    $parsedFieldIntake.evidence_root_verify -ne 'ok' -or
    $parsedFieldIntake.orphan_board_verified_records -ne '0' -or
    $parsedFieldIntake.ready_for_real_pins -ne 'no' -or
    $parsedFieldIntake.board_evidence_ready -ne 'no' -or
    $parsedFieldIntake.do_pwm_ready_rows -ne '16' -or
    $parsedFieldIntake.bus_tx_ready_rows -ne '2' -or
    $parsedFieldIntake.verified_wiring_evidence_files -ne '0' -or
    $parsedFieldIntake.board_validation_ready -ne 'no' -or
    $parsedFieldIntake.verified_board_validation_evidence_files -ne '0' -or
    $parsedFieldIntake.board_verified_evidence_contract -ne 'md_non_placeholder' -or
    $parsedFieldIntake.board_verified_attachment_contract -ne 'project_relative_existing_files' -or
    $parsedFieldIntake.pl_estop_safety_boundary -ne 'ok' -or
    $parsedFieldIntake.active_pending_wiring_pin_assignments -ne '0' -or
    $parsedFieldIntake.active_estop_gate_output_ports -ne '0') {
  throw 'Manifest PL E-stop field intake gate is not the expected local-only not_ready state'
}
if ($parsedRealPinPromotion.pl_estop_real_pin_promotion_gate -ne 'local_hard_gate_promoted' -or
    $parsedRealPinPromotion.active_promoted_wiring_assignments -ne '17' -or
    $parsedRealPinPromotion.active_promoted_do_pwm_assignments -ne '16' -or
    $parsedRealPinPromotion.active_promoted_estop_input_assignments -ne '1' -or
    $parsedRealPinPromotion.active_promoted_bus_tx_assignments -ne '0' -or
    $parsedRealPinPromotion.active_estop_gate_output_ports -ne '0' -or
    $parsedRealPinPromotion.local_hard_gate_promoted -ne 'yes' -or
    $parsedRealPinPromotion.promotion_requires_e11 -ne 'no' -or
    $parsedRealPinPromotion.e11_rtl_xdc_ready -ne 'no') {
  throw 'Manifest PL E-stop real-pin promotion gate is not in the expected local hard-gate promoted state'
}
if ($parsedReadiness.pl_estop_readiness -ne 'not_ready' -or
    $parsedReadiness.e11_rtl_xdc_ready -ne 'no' -or
    $parsedReadiness.a11_board_validation_ready -ne 'no' -or
    $parsedReadiness.pl_estop_hardware_evidence_request_verify -ne 'ok' -or
    $parsedReadiness.pl_estop_field_runbook_verify -ne 'ok' -or
    $parsedReadiness.field_runbook_state -ne 'open' -or
    $parsedReadiness.pl_estop_evidence_templates_verify -ne 'ok' -or
    $parsedReadiness.board_verified_template_records -ne '0' -or
    $parsedReadiness.pl_estop_field_intake -ne 'not_ready' -or
    $parsedReadiness.field_intake_structural_contract -ne 'ok') {
  throw 'Manifest PL E-stop readiness state is not the expected local-only not_ready state'
}
if ($parsedBoardInputHandoff.board_input_handoff -ne 'local_verified_only' -or
    $parsedBoardInputHandoff.board_input_handoff_software_handoff -ne 'board_inputs/software_handoff.md' -or
    $parsedBoardInputHandoff.board_input_handoff_hardware_abi_header -ne 'board_inputs/v3_hardware_abi.h' -or
    $parsedBoardInputHandoff.board_input_handoff_hardware_abi_header_verify -ne 'ok' -or
    $parsedBoardInputHandoff.board_input_handoff_driver_contract_header -ne 'board_inputs/z20_v1_5_hardware_abi.h' -or
    $parsedBoardInputHandoff.board_input_handoff_driver_contract_dtsi -ne 'board_inputs/z20_v1_5_axi_lite.dtsi' -or
    $parsedBoardInputHandoff.board_input_handoff_driver_contract_verify -ne 'ok' -or
    $parsedBoardInputHandoff.board_input_handoff_register_map -ne 'board_inputs/pl_estop_register_map.md' -or
    $parsedBoardInputHandoff.board_input_handoff_io_owner_register_map -ne 'docs/io_owner_register_map.md' -or
    $parsedBoardInputHandoff.board_input_handoff_register_map_verify -ne 'ok' -or
    $parsedBoardInputHandoff.board_input_handoff_active_xdc_traceability -ne 'ok' -or
    $parsedBoardInputHandoff.board_input_handoff_active_xdc_electrical_contract -ne 'ok' -or
    $parsedBoardInputHandoff.board_input_handoff_readiness -ne 'not_ready' -or
    $parsedBoardInputHandoff.board_input_handoff_pl_estop_sim -ne 'ok' -or
    $parsedBoardInputHandoff.board_input_handoff_timing_params -ne 'ok' -or
    $parsedBoardInputHandoff.board_input_handoff_request_verify -ne 'ok' -or
    $parsedBoardInputHandoff.board_input_handoff_field_packet_verify -ne 'ok' -or
    $parsedBoardInputHandoff.board_input_handoff_field_runbook_verify -ne 'ok' -or
    $parsedBoardInputHandoff.board_input_handoff_evidence_templates_verify -ne 'ok' -or
    $parsedBoardInputHandoff.board_input_handoff_evidence_root_verify -ne 'ok' -or
    $parsedBoardInputHandoff.board_input_handoff_output_shutdown_contract -ne 'code_review_only') {
  throw 'Manifest board-input handoff export state is not the expected local-only state'
}
if ($parsedBoardInputHandoffVerify.board_input_handoff_verify -ne 'ok' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_state -ne 'local_verified_only' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_software_handoff -ne 'board_inputs/software_handoff.md' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_hardware_abi_header -ne 'board_inputs/v3_hardware_abi.h' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_hardware_abi_header_verify -ne 'ok' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_driver_contract_header -ne 'board_inputs/z20_v1_5_hardware_abi.h' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_driver_contract_dtsi -ne 'board_inputs/z20_v1_5_axi_lite.dtsi' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_driver_contract_verify -ne 'ok' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_register_map -ne 'board_inputs/pl_estop_register_map.md' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_io_owner_register_map -ne 'docs/io_owner_register_map.md' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_register_map_verify -ne 'ok' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_active_xdc_traceability -ne 'ok' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_active_xdc_electrical_contract -ne 'ok' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_readiness -ne 'not_ready' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_pl_estop_sim -ne 'ok' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_timing_params -ne 'ok' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_request_verify -ne 'ok' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_field_packet_verify -ne 'ok' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_field_runbook_verify -ne 'ok' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_evidence_templates_verify -ne 'ok' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_evidence_root_verify -ne 'ok' -or
    $parsedBoardInputHandoffVerify.board_input_handoff_output_shutdown_contract -ne 'code_review_only') {
  throw 'Manifest board-input handoff verify check is not closed'
}
if ($parsedRemaining.csv_rows -ne '0' -or $parsedRemaining.csv_matches_check_active -ne 'yes') {
  throw 'Manifest remaining DRC CSV check is not closed'
}
if ($parsedConflicts.active_pin_conflicts -ne '0') {
  throw "Manifest active_pin_conflicts is not zero: $($parsedConflicts.active_pin_conflicts)"
}

$artifactCount = 0
$artifactPaths = @()
foreach ($artifact in @($manifest.artifacts)) {
  $artifactPaths += $artifact.path
  $item = Get-ProjectFile -RootDir $ProjectDir -RelativePath $artifact.path
  $artifactCount += 1
  if ([int64]$artifact.bytes -ne [int64]$item.Length) {
    throw "Artifact size mismatch: $($artifact.path)"
  }
  $hash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($hash -ne $artifact.sha256) {
    throw "Artifact sha256 mismatch: $($artifact.path)"
  }
}
if ($artifactPaths -notcontains 'board_inputs/software_handoff.md') {
  throw 'Manifest is missing board_inputs/software_handoff.md'
}
if ($artifactPaths -notcontains 'board_inputs/v3_hardware_abi.h') {
  throw 'Manifest is missing board_inputs/v3_hardware_abi.h'
}
if ($artifactPaths -notcontains 'board_inputs/z20_v1_5_hardware_abi.h') {
  throw 'Manifest is missing board_inputs/z20_v1_5_hardware_abi.h'
}
if ($artifactPaths -notcontains 'board_inputs/z20_v1_5_axi_lite.dtsi') {
  throw 'Manifest is missing board_inputs/z20_v1_5_axi_lite.dtsi'
}
if ($artifactPaths -notcontains 'board_inputs/pl_estop_register_map.md') {
  throw 'Manifest is missing board_inputs/pl_estop_register_map.md'
}
if ($artifactPaths -notcontains 'docs/io_owner_register_map.md') {
  throw 'Manifest is missing docs/io_owner_register_map.md'
}

Write-Output 'manifest=board_inputs/manifest.json'
Write-Output "schema=$($manifest.schema)"
Write-Output "board_closure_state=$($manifest.board_closure_state)"
Write-Output "artifact_count=$artifactCount"
Write-Output 'board_software_handoff_artifact=present'
Write-Output 'hardware_abi_header_artifact=present'
Write-Output 'driver_contract_header_artifact=present'
Write-Output 'driver_contract_dtsi_artifact=present'
Write-Output 'pl_estop_register_map_artifact=present'
Write-Output 'io_owner_register_map_artifact=present'
Write-Output "hashes=ok"
Write-Output "build_status=$($manifest.timing_latest.build_status)"
Write-Output "timing_status=$($manifest.timing_latest.timing_status)"
Write-Output "timing_wns=$($manifest.timing_latest.wns)"
Write-Output "timing_margin_policy=$($parsedVivadoCleanliness.timing_margin_policy)"
Write-Output "timing_margin_target_status=$($parsedVivadoCleanliness.timing_margin_target_status)"
Write-Output "unassigned_top_ports_count=$($parsedActive.unassigned_top_ports_count)"
Write-Output "active_xdc_traceability=$($parsedTraceability.active_xdc_traceability)"
Write-Output "active_xdc_electrical_contract=$($parsedElectrical.active_xdc_electrical_contract)"
Write-Output "vivado_xsa_cleanliness=$($parsedVivadoCleanliness.vivado_xsa_cleanliness)"
Write-Output "hardware_abi_header=$($parsedHardwareAbiVerify.hardware_abi_header)"
Write-Output "hardware_abi_step_ip_base=$($parsedHardwareAbiVerify.step_ip_base)"
Write-Output "hardware_abi_pl_estop_base=$($parsedHardwareAbiVerify.pl_estop_base)"
Write-Output "hardware_abi_io_owner_base=$($parsedHardwareAbiVerify.io_owner_base)"
Write-Output "driver_contract=$($parsedDriverContractVerify.driver_contract)"
Write-Output "driver_contract_header=$($parsedDriverContractVerify.header)"
Write-Output "driver_contract_dtsi=$($parsedDriverContractVerify.dtsi)"
Write-Output "driver_contract_pl_estop_irq_f2p_bit=$($parsedDriverContractVerify.pl_estop_irq_f2p_bit)"
Write-Output "driver_contract_pl_estop_irq_gic_spi_cell=$($parsedDriverContractVerify.pl_estop_irq_gic_spi_cell)"
Write-Output "active_constraints_loaded=$($parsedVivadoCleanliness.active_constraints_loaded)"
Write-Output "truth_source_xdc_loaded=$($parsedVivadoCleanliness.truth_source_xdc_loaded)"
Write-Output "drc_blocking_rules=$($parsedVivadoCleanliness.drc_blocking_rules)"
Write-Output "drc_allowed_warning_rules=$($parsedVivadoCleanliness.drc_allowed_warning_rules)"
Write-Output "vivado_warning_summary=$($parsedVivadoWarningSummary.vivado_warning_summary)"
Write-Output "vivado_warning_summary_verify=$($parsedVivadoWarningSummaryVerify.vivado_warning_summary_verify)"
Write-Output "vivado_warning_lines=$($parsedVivadoWarningSummary.vivado_warning_lines)"
Write-Output "vivado_warning_codes=$($parsedVivadoWarningSummary.vivado_warning_codes)"
Write-Output "constraint_truth_warning_lines=$($parsedVivadoWarningSummary.constraint_truth_warning_lines)"
Write-Output "retired_hdmi_warning_lines=$($parsedVivadoWarningSummary.retired_hdmi_warning_lines)"
Write-Output "active_pin_conflicts=$($parsedConflicts.active_pin_conflicts)"
Write-Output "project_independence=$($parsedIndependence.project_independence)"
Write-Output "project_portability=$($parsedPortability.project_portability)"
Write-Output "absolute_path_scan=$($parsedPortability.absolute_path_scan)"
Write-Output "tmp_files=$($parsedPortability.tmp_files)"
Write-Output "adc_mapping=$($parsedAdc.adc_mapping)"
Write-Output "adc_owner=$($parsedAdc.adc_owner)"
Write-Output "adc_xadc_pins=$($parsedAdc.adc_xadc_pins)"
Write-Output "adc_spi_mapping=$($parsedAdc.adc_spi_mapping)"
Write-Output "xadc_one_channel_adc=$($parsedAdc.xadc_one_channel_adc)"
Write-Output "legacy_axis_adc_boundary=$($parsedLegacyBoundary.legacy_axis_adc_boundary)"
Write-Output "wrapper_axis_boundary=$($parsedLegacyBoundary.wrapper_axis_boundary)"
Write-Output "axis_functional_completion=$($parsedLegacyBoundary.axis_functional_completion)"
Write-Output "axis_motion_owner=$($parsedLegacyBoundary.axis_motion_owner)"
Write-Output "axis_ena_owner=$($parsedLegacyBoundary.axis_ena_owner)"
Write-Output "axis_78_encoder_processing=$($parsedLegacyBoundary.axis_78_encoder_processing)"
Write-Output "di_mpg_alarm_processing=$($parsedLegacyBoundary.di_mpg_alarm_processing)"
Write-Output "do_pwm_normal_owner=$($parsedLegacyBoundary.do_pwm_normal_owner)"
Write-Output "rs485_boundary=$($parsedLegacyBoundary.rs485_boundary)"
Write-Output "touch_int_rst_boundary=$($parsedLegacyBoundary.touch_int_rst_boundary)"
Write-Output "z20_v15_io_owner_sim=$($parsedIoOwnerSim.z20_v15_io_owner_sim)"
Write-Output "pl_estop_sim=$($parsedSim.pl_estop_sim)"
Write-Output "pl_estop_timing_params=$($parsedTimingParams.pl_estop_timing_params)"
Write-Output "pl_estop_safety_boundary=$($parsedEstopBoundary.pl_estop_safety_boundary)"
Write-Output "pl_estop_output_shutdown_contract=$($parsedOutputShutdown.pl_estop_output_shutdown_contract)"
Write-Output "pl_estop_bus_gate_owner=$($parsedBusGateOwner.pl_estop_bus_gate_owner)"
Write-Output "pl_estop_register_map=$($parsedRegisterMap.pl_estop_register_map)"
Write-Output "bus_gate_transport=$($parsedBusGateOwner.production_transport)"
Write-Output "bus_gate_board_evidence=$($parsedBusGateOwner.board_evidence_state)"
Write-Output "active_estop_input_pin_assignments=$($parsedEstopBoundary.active_estop_input_pin_assignments)"
Write-Output "active_pending_wiring_pin_assignments=$($parsedEstopBoundary.active_pending_wiring_pin_assignments)"
Write-Output "pl_estop_wiring_evidence=$($parsedWiringEvidence.pl_estop_wiring_evidence)"
Write-Output "pl_estop_wiring_evidence_contract=$($parsedWiringEvidence.board_verified_evidence_contract)"
Write-Output "pl_estop_wiring_attachment_contract=$($parsedWiringEvidence.board_verified_attachment_contract)"
Write-Output "pl_estop_board_validation=$($parsedBoardValidation.pl_estop_board_validation)"
Write-Output "pl_estop_board_validation_contract=$($parsedBoardValidation.board_verified_evidence_contract)"
Write-Output "pl_estop_board_validation_attachment_contract=$($parsedBoardValidation.board_verified_attachment_contract)"
Write-Output "pl_estop_evidence_gap=$($parsedEvidenceGap.pl_estop_evidence_gap)"
Write-Output "pl_estop_hardware_evidence_request=$($parsedHardwareEvidenceRequest.pl_estop_hardware_evidence_request)"
Write-Output "pl_estop_hardware_evidence_request_verify=$($parsedHardwareEvidenceRequestVerify.pl_estop_hardware_evidence_request_verify)"
Write-Output "pl_estop_field_packet=$($parsedFieldPacket.pl_estop_field_packet)"
Write-Output "pl_estop_field_packet_verify=$($parsedFieldPacketVerify.pl_estop_field_packet_verify)"
Write-Output "pl_estop_field_runbook_verify=$($parsedFieldRunbook.pl_estop_field_runbook_verify)"
Write-Output "pl_estop_evidence_templates=$($parsedEvidenceTemplates.pl_estop_evidence_templates)"
Write-Output "pl_estop_evidence_templates_verify=$($parsedEvidenceTemplatesVerify.pl_estop_evidence_templates_verify)"
Write-Output "pl_estop_evidence_root_verify=$($parsedEvidenceRoot.pl_estop_evidence_root_verify)"
Write-Output "pl_estop_orphan_board_verified_records=$($parsedEvidenceRoot.orphan_board_verified_records)"
Write-Output "pl_estop_field_intake=$($parsedFieldIntake.pl_estop_field_intake)"
Write-Output "pl_estop_field_intake_contract=$($parsedFieldIntake.field_intake_structural_contract)"
Write-Output "pl_estop_real_pin_promotion_gate=$($parsedRealPinPromotion.pl_estop_real_pin_promotion_gate)"
Write-Output "pl_estop_readiness=$($parsedReadiness.pl_estop_readiness)"
Write-Output "board_input_handoff=$($parsedBoardInputHandoff.board_input_handoff)"
Write-Output "board_input_handoff_verify=$($parsedBoardInputHandoffVerify.board_input_handoff_verify)"
Write-Output "board_input_handoff_active_xdc_electrical_contract=$($parsedBoardInputHandoffVerify.board_input_handoff_active_xdc_electrical_contract)"
