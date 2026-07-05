param(
  [string]$ProjectDir,
  [string]$HandoffPath,
  [switch]$StaticOnly
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($HandoffPath)) {
  $HandoffPath = Join-Path $ProjectDir 'board_inputs/README.md'
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

function Assert-TextContains {
  param(
    [string]$Text,
    [string]$Pattern,
    [string]$Label
  )

  if ($Text -notmatch $Pattern) {
    throw "Missing board-input handoff content: $Label"
  }
}

if (-not (Test-Path -LiteralPath $HandoffPath)) {
  throw 'Missing board_inputs/README.md'
}

$text = Get-Content -LiteralPath $HandoffPath -Raw -Encoding UTF8
if ($text -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
  throw 'Board-input handoff contains an absolute path'
}
if ($text -match 'vivado_hw_project') {
  throw 'Board-input handoff contains an old-project reference'
}

if ($StaticOnly) {
  $traceability = [ordered]@{
    active_xdc_traceability = 'ok'
    traced_assignments = '180'
  }
  $electrical = [ordered]@{
    active_xdc_electrical_contract = 'ok'
    iostandard_assignments = '180'
    lvcmos33_assignments = '180'
  }
  $vivadoCleanliness = [ordered]@{
    vivado_xsa_cleanliness = 'ok'
    active_constraints_loaded = 'mapped_only'
    truth_source_xdc_loaded = 'no'
    old_project_xdc_loaded = 'no'
    drc_blocking_rules = '0'
    drc_allowed_warning_rules = 'RTSTAT-10'
  }
  $abiHeader = [ordered]@{
    hardware_abi_header = 'ok'
    output = 'board_inputs/v3_hardware_abi.h'
    step_ip_base = '0x43CB0000'
    pl_estop_base = '0x41260000'
    io_owner_base = '0x41270000'
  }
  $driverContract = [ordered]@{
    driver_contract = 'ok'
    header = 'board_inputs/z20_v1_5_hardware_abi.h'
    dtsi = 'board_inputs/z20_v1_5_axi_lite.dtsi'
    pl_estop_irq_f2p_bit = '14'
    pl_estop_irq_gic_spi_cell = '43'
  }
  $warningSummaryVerify = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_vivado_warning_summary.ps1'
  $warningSummary = [ordered]@{
    vivado_warning_summary = 'classified'
    vivado_warning_lines = $warningSummaryVerify.vivado_warning_lines
    vivado_warning_codes = $warningSummaryVerify.vivado_warning_codes
    constraint_truth_warning_lines = $warningSummaryVerify.constraint_truth_warning_lines
    retired_hdmi_warning_lines = $warningSummaryVerify.retired_hdmi_warning_lines
  }
  $readiness = [ordered]@{
    pl_estop_readiness = 'not_ready'
    readiness_blockers = 'pl_estop_wiring_evidence_not_ready,pl_estop_board_evidence_not_ready,pl_estop_board_validation_not_ready'
  }
  $portability = [ordered]@{
    project_portability = 'ok'
    absolute_path_scan = 'ok'
    manifest_relative_paths = 'ok'
  }
  $adc = [ordered]@{
    adc_mapping = 'ok'
    adc_owner = 'XADC_VP_VN_ONE_CHANNEL'
    adc_xadc_pins = 'L11,M12'
    adc_spi_mapping = 'retired'
  }
  $legacyBoundary = [ordered]@{
    legacy_axis_adc_boundary = 'retired'
    wrapper_axis_boundary = 'current_8bit'
    axis_functional_completion = 'vivado_io_owner_connected'
    axis_motion_owner = 'step_ip_8axis_stepdir_encoder_direct'
    axis_ena_owner = 'z20_v15_io_owner_axi_lite'
    axis_78_encoder_processing = 'connected_to_step_ip'
    di_mpg_alarm_processing = 'z20_v15_io_owner_input_registers'
    do_pwm_normal_owner = 'z20_v15_io_owner_do_pwm'
    rs485_boundary = 'exported_ps_uart1_emio'
    touch_int_rst_boundary = 'z20_v15_io_owner_tp_int_rst'
  }
  $ioOwnerSim = [ordered]@{
    z20_v15_io_owner_sim = 'ok'
    z20_v15_io_owner_axi_lite_tb = 'pass'
    sim_outputs = 'persistent_none'
  }
  $sim = [ordered]@{
    pl_estop_sim = 'ok'
    pl_estop_core_tb = 'pass'
    pl_estop_axi_lite_tb = 'pass'
  }
  $timingParams = [ordered]@{
    pl_estop_timing_params = 'ok'
    clock_hz = '100000000'
    debounce_ms = '10'
    debounce_cycles = '1000000'
    brake_lead_us = '50'
    brake_cycles = '5000'
    axis_count = '8'
    z_axis_index = '2'
  }
  $hardwareRequest = [ordered]@{
    pl_estop_hardware_evidence_request_verify = 'ok'
    hardware_evidence_request_state = 'open'
  }
  $fieldPacket = [ordered]@{
    pl_estop_field_packet_verify = 'ok'
    field_packet_state = 'open'
  }
  $fieldRunbook = [ordered]@{
    pl_estop_field_runbook_verify = 'ok'
    field_runbook_state = 'open'
  }
  $evidenceTemplates = [ordered]@{
    pl_estop_evidence_templates_verify = 'ok'
    template_state = 'open'
  }
  $evidenceRoot = [ordered]@{
    pl_estop_evidence_root_verify = 'ok'
    orphan_board_verified_records = '0'
  }
  $fieldIntake = [ordered]@{
    pl_estop_field_intake = 'not_ready'
    field_intake_structural_contract = 'ok'
    field_intake_blockers = 'wiring_not_ready_for_real_pins,wiring_board_evidence_not_ready,board_validation_not_ready'
  }
  $realPinPromotion = [ordered]@{
    pl_estop_real_pin_promotion_gate = 'local_hard_gate_promoted'
    active_promoted_wiring_assignments = '17'
    active_promoted_do_pwm_assignments = '16'
    active_promoted_bus_tx_assignments = '0'
    promotion_requires_e11 = 'no'
  }
  $outputShutdown = [ordered]@{
    pl_estop_output_shutdown_contract = 'code_review_only'
    do_pwm_wiring_rows = '16'
    bus_tx_wiring_rows = '2'
  }
  $busGateOwner = [ordered]@{
    pl_estop_bus_gate_owner = 'ps_gem1_emio_rgmii_local_verified'
    production_transport = 'EtherCAT over PS GEM1/EMIO'
    board_evidence_state = 'pending'
  }
  $registerMap = [ordered]@{
    pl_estop_register_map = 'ok'
    registers = '9'
    status_bits = '9'
    control_bits = '2'
  }
  $safety = [ordered]@{
    pl_estop_safety_boundary = 'ok'
  }
} else {
  $traceability = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_active_xdc_traceability.ps1'
  $electrical = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_active_xdc_electrical_contract.ps1'
  $vivadoCleanliness = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_vivado_xsa_cleanliness.ps1'
  $abiHeader = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_hardware_abi_header.ps1'
  $driverContract = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_driver_contract.ps1'
  $warningSummaryVerify = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_vivado_warning_summary.ps1'
  $warningSummary = [ordered]@{
    vivado_warning_summary = 'classified'
    vivado_warning_lines = $warningSummaryVerify.vivado_warning_lines
    vivado_warning_codes = $warningSummaryVerify.vivado_warning_codes
    constraint_truth_warning_lines = $warningSummaryVerify.constraint_truth_warning_lines
    retired_hdmi_warning_lines = $warningSummaryVerify.retired_hdmi_warning_lines
  }
  $readiness = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_readiness.ps1'
  $portability = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_project_portability.ps1'
  $adc = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_adc_spi_mapping.ps1'
  $legacyBoundary = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_no_legacy_axis_adc_boundary.ps1'
  $ioOwnerSim = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_z20_v15_io_owner_sim.ps1'
  $sim = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_sim.ps1'
  $timingParams = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_timing_params.ps1'
  $hardwareRequest = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_hardware_evidence_request.ps1'
  $fieldPacket = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_field_packet.ps1'
  $fieldRunbook = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_field_runbook.ps1'
  $evidenceTemplates = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_evidence_templates.ps1'
  $evidenceRoot = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_evidence_root.ps1'
  $fieldIntake = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_field_intake.ps1'
  $realPinPromotion = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_real_pin_promotion_gate.ps1'
  $outputShutdown = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_output_shutdown_contract.ps1'
  $busGateOwner = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_bus_gate_owner.ps1'
  $registerMap = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_register_map.ps1'
  $safety = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_safety_boundary.ps1'
}
$handoffRel = Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $HandoffPath
$traceabilityText = 'Active XDC traceability: `' + $traceability.active_xdc_traceability + '`'
$tracedAssignmentsText = 'Active XDC traced assignments: `' + $traceability.traced_assignments + '`'
$electricalText = 'Active XDC electrical contract: `' + $electrical.active_xdc_electrical_contract + '`'
$iostandardAssignmentsText = 'Active XDC IOSTANDARD assignments: `' + $electrical.iostandard_assignments + '`'
$lvcmos33AssignmentsText = 'Active XDC LVCMOS33 assignments: `' + $electrical.lvcmos33_assignments + '`'
$vivadoCleanlinessText = 'Vivado/XSA cleanliness: `' + $vivadoCleanliness.vivado_xsa_cleanliness + '`'
$activeConstraintsLoadedText = 'Active constraints loaded: `' + $vivadoCleanliness.active_constraints_loaded + '`'
$truthXdcLoadedText = 'Truth/source XDC loaded by Vivado: `' + $vivadoCleanliness.truth_source_xdc_loaded + '`'
$oldXdcLoadedText = 'Old project XDC loaded by Vivado: `' + $vivadoCleanliness.old_project_xdc_loaded + '`'
$drcBlockingText = 'DRC blocking rules: `' + $vivadoCleanliness.drc_blocking_rules + '`'
$drcAllowedWarningsText = 'DRC allowed warning rules: `' + $vivadoCleanliness.drc_allowed_warning_rules + '`'
$abiHeaderText = 'Hardware ABI header: `' + $abiHeader.hardware_abi_header + '`'
$abiHeaderOutputText = 'Hardware ABI header output: `' + $abiHeader.output + '`'
$abiHeaderAddressText = 'Hardware ABI address anchors: step_ip `' + $abiHeader.step_ip_base + '` / pl_estop `' + $abiHeader.pl_estop_base + '` / io_owner `' + $abiHeader.io_owner_base + '`'
$driverContractText = 'Driver contract: `' + $driverContract.driver_contract + '`'
$driverContractHeaderText = 'Driver contract header: `' + $driverContract.header + '`'
$driverContractDtsiText = 'Driver contract DTSI: `' + $driverContract.dtsi + '`'
$driverContractIrqText = 'PL E-stop DTS interrupt: IRQ_F2P[`' + $driverContract.pl_estop_irq_f2p_bit + '`] / GIC SPI cell `' + $driverContract.pl_estop_irq_gic_spi_cell + '`'
$warningSummaryText = 'Vivado warning summary: `' + $warningSummary.vivado_warning_summary + '`'
$warningSummaryVerifyText = 'Vivado warning summary verify: `' + $warningSummaryVerify.vivado_warning_summary_verify + '`'
$warningLinesText = 'Vivado warning lines: `' + $warningSummary.vivado_warning_lines + '`'
$warningCodesText = 'Vivado warning codes: `' + $warningSummary.vivado_warning_codes + '`'
$constraintWarningLinesText = 'Constraint/source warning lines: `' + $warningSummary.constraint_truth_warning_lines + '`'
$retiredHdmiWarningLinesText = 'Retired HDMI warning lines: `' + $warningSummary.retired_hdmi_warning_lines + '`'
$portabilityText = 'Project portability: `' + $portability.project_portability + '`'
$absolutePathText = 'Absolute path scan: `' + $portability.absolute_path_scan + '`'
$manifestRelativeText = 'Manifest relative paths: `' + $portability.manifest_relative_paths + '`'
$adcText = 'ADC mapping: `' + $adc.adc_mapping + '`'
$adcOwnerText = 'ADC owner: `' + $adc.adc_owner + '`'
$adcXadcPinsText = 'ADC XADC pins: `' + $adc.adc_xadc_pins + '`'
$adcSpiText = 'ADC SPI mapping: `' + $adc.adc_spi_mapping + '`'
$legacyBoundaryText = 'Legacy axis/ADC external boundary: `' + $legacyBoundary.legacy_axis_adc_boundary + '`'
$wrapperAxisBoundaryText = 'Wrapper axis boundary: `' + $legacyBoundary.wrapper_axis_boundary + '`'
$axisFunctionalCompletionText = 'Axis functional completion: `' + $legacyBoundary.axis_functional_completion + '`'
$axisMotionOwnerText = 'Axis motion owner: `' + $legacyBoundary.axis_motion_owner + '`'
$axisEnaOwnerText = 'Axis ENA owner: `' + $legacyBoundary.axis_ena_owner + '`'
$axis78EncoderText = 'Axis 7/8 encoder processing: `' + $legacyBoundary.axis_78_encoder_processing + '`'
$diMpgAlarmText = 'DI/MPG/ALM processing: `' + $legacyBoundary.di_mpg_alarm_processing + '`'
$doPwmOwnerText = 'DO/PWM normal owner: `' + $legacyBoundary.do_pwm_normal_owner + '`'
$rs485BoundaryText = 'RS485 boundary: `' + $legacyBoundary.rs485_boundary + '`'
$touchBoundaryText = 'Touch INT/RST boundary: `' + $legacyBoundary.touch_int_rst_boundary + '`'
$ioOwnerSimText = 'Z20 v1.5 IO owner simulation: `' + $ioOwnerSim.z20_v15_io_owner_sim + '`'
$simText = 'PL E-stop simulation: `' + $sim.pl_estop_sim + '`'
$simCoreText = 'PL E-stop core testbench: `' + $sim.pl_estop_core_tb + '`'
$simAxiText = 'PL E-stop AXI testbench: `' + $sim.pl_estop_axi_lite_tb + '`'
$timingParamsText = 'PL E-stop timing params: `' + $timingParams.pl_estop_timing_params + '`'
$timingClockText = 'PL E-stop timing clock/debounce/brake: `' + $timingParams.clock_hz + '` / `' + $timingParams.debounce_ms + '` ms / `' + $timingParams.brake_lead_us + '` us'
$timingCyclesText = 'PL E-stop derived cycles: `' + $timingParams.debounce_cycles + '` debounce / `' + $timingParams.brake_cycles + '` brake'
$timingAxisText = 'PL E-stop axis config: `' + $timingParams.axis_count + '` axes / Z index `' + $timingParams.z_axis_index + '`'
$readinessText = 'PL E-stop readiness: `' + $readiness.pl_estop_readiness + '`'
$blockersText = 'Readiness blockers: `' + $readiness.readiness_blockers + '`'
$requestVerifyText = 'Hardware evidence request verify: `' + $hardwareRequest.pl_estop_hardware_evidence_request_verify + '`'
$requestStateText = 'Hardware evidence request state: `' + $hardwareRequest.hardware_evidence_request_state + '`'
$fieldPacketVerifyText = 'Field evidence packet verify: `' + $fieldPacket.pl_estop_field_packet_verify + '`'
$fieldPacketStateText = 'Field evidence packet state: `' + $fieldPacket.field_packet_state + '`'
$fieldRunbookVerifyText = 'Field execution runbook verify: `' + $fieldRunbook.pl_estop_field_runbook_verify + '`'
$fieldRunbookStateText = 'Field execution runbook state: `' + $fieldRunbook.field_runbook_state + '`'
$evidenceTemplatesVerifyText = 'Evidence record templates verify: `' + $evidenceTemplates.pl_estop_evidence_templates_verify + '`'
$evidenceTemplatesStateText = 'Evidence record templates state: `' + $evidenceTemplates.template_state + '`'
$evidenceRootVerifyText = 'Evidence root verify: `' + $evidenceRoot.pl_estop_evidence_root_verify + '`'
$orphanRecordsText = 'Orphan board-verified records: `' + $evidenceRoot.orphan_board_verified_records + '`'
$fieldIntakeText = 'Field intake gate: `' + $fieldIntake.pl_estop_field_intake + '`'
$fieldIntakeContractText = 'Field intake structural contract: `' + $fieldIntake.field_intake_structural_contract + '`'
$fieldIntakeBlockersText = 'Field intake blockers: `' + $fieldIntake.field_intake_blockers + '`'
$realPinPromotionText = 'Real pin promotion gate: `' + $realPinPromotion.pl_estop_real_pin_promotion_gate + '`'
$outputShutdownText = 'Output shutdown contract: `' + $outputShutdown.pl_estop_output_shutdown_contract + '`'
$outputShutdownDoPwmRowsText = 'Output shutdown DO/PWM rows: `' + $outputShutdown.do_pwm_wiring_rows + '`'
$outputShutdownBusTxRowsText = 'Output shutdown bus TX rows: `' + $outputShutdown.bus_tx_wiring_rows + '`'
$busGateOwnerText = 'Bus gate owner: `' + $busGateOwner.pl_estop_bus_gate_owner + '`'
$busGateTransportText = 'Bus gate transport: `' + $busGateOwner.production_transport + '`'
$busGateBoardEvidenceText = 'Bus gate board evidence: `' + $busGateOwner.board_evidence_state + '`'
$registerMapText = 'PL E-stop register map: `' + $registerMap.pl_estop_register_map + '`'
$registerCountText = 'PL E-stop register count: `' + $registerMap.registers + '`'
$registerBitsText = 'PL E-stop status/control bits: `' + $registerMap.status_bits + '` / `' + $registerMap.control_bits + '`'
$activePromotedWiringText = 'Active promoted wiring assignments: `' + $realPinPromotion.active_promoted_wiring_assignments + '`'
$activePromotedDoPwmText = 'Active promoted DO/PWM assignments: `' + $realPinPromotion.active_promoted_do_pwm_assignments + '`'
$activePromotedBusTxText = 'Active promoted bus TX assignments: `' + $realPinPromotion.active_promoted_bus_tx_assignments + '`'
$promotionRequiresE11Text = 'Promotion requires E11: `' + $realPinPromotion.promotion_requires_e11 + '`'
$safetyText = 'PL E-stop safety boundary: `' + $safety.pl_estop_safety_boundary + '`'

Assert-TextContains -Text $text -Pattern '# Z20 v1.5 Board Input Handoff' -Label 'title'
Assert-TextContains -Text $text -Pattern 'Board closure state: `local_verified_only`' -Label 'local-only closure state'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($traceabilityText)) -Label 'active XDC traceability'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($tracedAssignmentsText)) -Label 'active XDC traced assignments'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($electricalText)) -Label 'active XDC electrical contract'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($iostandardAssignmentsText)) -Label 'active XDC IOSTANDARD assignments'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($lvcmos33AssignmentsText)) -Label 'active XDC LVCMOS33 assignments'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($vivadoCleanlinessText)) -Label 'Vivado XSA cleanliness'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($activeConstraintsLoadedText)) -Label 'active constraints loaded'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($truthXdcLoadedText)) -Label 'truth XDC not loaded'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($oldXdcLoadedText)) -Label 'old XDC not loaded'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($drcBlockingText)) -Label 'DRC blocking rules'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($drcAllowedWarningsText)) -Label 'DRC allowed warning rules'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($abiHeaderText)) -Label 'hardware ABI header state'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($abiHeaderOutputText)) -Label 'hardware ABI header output'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($abiHeaderAddressText)) -Label 'hardware ABI address anchors'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($driverContractText)) -Label 'driver contract state'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($driverContractHeaderText)) -Label 'driver contract header'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($driverContractDtsiText)) -Label 'driver contract DTSI'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($driverContractIrqText)) -Label 'driver contract IRQ'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($warningSummaryText)) -Label 'Vivado warning summary'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($warningSummaryVerifyText)) -Label 'Vivado warning summary verify'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($warningLinesText)) -Label 'Vivado warning lines'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($warningCodesText)) -Label 'Vivado warning codes'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($constraintWarningLinesText)) -Label 'constraint warning lines'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($retiredHdmiWarningLinesText)) -Label 'retired HDMI warning lines'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($portabilityText)) -Label 'project portability'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($absolutePathText)) -Label 'absolute path scan'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($manifestRelativeText)) -Label 'manifest relative paths'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($adcText)) -Label 'ADC mapping'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($adcOwnerText)) -Label 'ADC owner'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($adcXadcPinsText)) -Label 'ADC XADC pins'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($adcSpiText)) -Label 'ADC SPI retired mapping'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($legacyBoundaryText)) -Label 'legacy axis ADC boundary'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($wrapperAxisBoundaryText)) -Label 'wrapper axis boundary'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($axisFunctionalCompletionText)) -Label 'axis functional completion'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($axisMotionOwnerText)) -Label 'axis motion owner'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($axisEnaOwnerText)) -Label 'axis ENA owner'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($axis78EncoderText)) -Label 'axis 7/8 encoder processing'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($diMpgAlarmText)) -Label 'DI/MPG/ALM processing'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($doPwmOwnerText)) -Label 'DO/PWM normal owner'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($rs485BoundaryText)) -Label 'RS485 boundary'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($touchBoundaryText)) -Label 'touch INT/RST boundary'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($ioOwnerSimText)) -Label 'Z20 v1.5 IO owner simulation'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($simText)) -Label 'PL E-stop simulation'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($simCoreText)) -Label 'PL E-stop core testbench'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($simAxiText)) -Label 'PL E-stop AXI testbench'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($timingParamsText)) -Label 'PL E-stop timing params'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($timingClockText)) -Label 'PL E-stop timing clock'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($timingCyclesText)) -Label 'PL E-stop timing cycles'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($timingAxisText)) -Label 'PL E-stop timing axis config'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($readinessText)) -Label 'readiness state'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($blockersText)) -Label 'readiness blockers'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($requestVerifyText)) -Label 'hardware request verify'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($requestStateText)) -Label 'hardware request state'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($fieldPacketVerifyText)) -Label 'field packet verify'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($fieldPacketStateText)) -Label 'field packet state'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($fieldRunbookVerifyText)) -Label 'field runbook verify'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($fieldRunbookStateText)) -Label 'field runbook state'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($evidenceTemplatesVerifyText)) -Label 'evidence templates verify'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($evidenceTemplatesStateText)) -Label 'evidence templates state'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($evidenceRootVerifyText)) -Label 'evidence root verify'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($orphanRecordsText)) -Label 'orphan board-verified records'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($fieldIntakeText)) -Label 'field intake gate'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($fieldIntakeContractText)) -Label 'field intake structural contract'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($fieldIntakeBlockersText)) -Label 'field intake blockers'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($realPinPromotionText)) -Label 'real pin promotion gate'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($outputShutdownText)) -Label 'output shutdown contract'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($outputShutdownDoPwmRowsText)) -Label 'output shutdown DO/PWM rows'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($outputShutdownBusTxRowsText)) -Label 'output shutdown bus TX rows'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($busGateOwnerText)) -Label 'bus gate owner'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($busGateTransportText)) -Label 'bus gate transport'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($busGateBoardEvidenceText)) -Label 'bus gate board evidence'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($registerMapText)) -Label 'PL E-stop register map'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($registerCountText)) -Label 'PL E-stop register count'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($registerBitsText)) -Label 'PL E-stop register status/control bits'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($activePromotedWiringText)) -Label 'active promoted wiring assignments'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($activePromotedDoPwmText)) -Label 'active promoted DO/PWM assignments'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($activePromotedBusTxText)) -Label 'active promoted bus TX assignments'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($promotionRequiresE11Text)) -Label 'promotion requires E11'
Assert-TextContains -Text $text -Pattern ([regex]::Escape($safetyText)) -Label 'safety boundary'
Assert-TextContains -Text $text -Pattern 'board_inputs/system\.xsa' -Label 'XSA path'
Assert-TextContains -Text $text -Pattern 'board_inputs/software_handoff\.md' -Label 'board software handoff path'
Assert-TextContains -Text $text -Pattern 'board_inputs/v3_hardware_abi\.h' -Label 'hardware ABI header path'
Assert-TextContains -Text $text -Pattern 'board_inputs/z20_v1_5_hardware_abi\.h' -Label 'driver contract header path'
Assert-TextContains -Text $text -Pattern 'board_inputs/z20_v1_5_axi_lite\.dtsi' -Label 'driver contract DTSI path'
Assert-TextContains -Text $text -Pattern 'board_inputs/pl_estop_register_map\.md' -Label 'PL E-stop register map path'
Assert-TextContains -Text $text -Pattern 'docs/io_owner_register_map\.md' -Label 'Z20 v1.5 IO owner register map path'
Assert-TextContains -Text $text -Pattern 'z20_v1_5_hw_project\.runs/impl_1/system_top\.bit' -Label 'bitstream path'
Assert-TextContains -Text $text -Pattern 'docs/pl_estop_hardware_evidence_request\.md' -Label 'hardware evidence request path'
Assert-TextContains -Text $text -Pattern 'docs/pl_estop_field_packet\.md' -Label 'field evidence packet path'
Assert-TextContains -Text $text -Pattern 'docs/pl_estop_field_execution_runbook\.md' -Label 'field execution runbook path'
Assert-TextContains -Text $text -Pattern 'docs/pl_estop_evidence_record_templates\.md' -Label 'evidence templates path'
Assert-TextContains -Text $text -Pattern 'docs/vivado_warning_summary\.csv' -Label 'Vivado warning summary CSV path'
Assert-TextContains -Text $text -Pattern 'docs/vivado_warning_summary\.md' -Label 'Vivado warning summary report path'
Assert-TextContains -Text $text -Pattern 'docs/evidence/pl_estop/README\.md' -Label 'evidence root policy path'
Assert-TextContains -Text $text -Pattern 'scripts/verify_board_input_manifest\.ps1' -Label 'manifest verification instruction'
Assert-TextContains -Text $text -Pattern 'Use `board_inputs/software_handoff\.md` as the board-software-facing summary' -Label 'board software handoff instruction'
Assert-TextContains -Text $text -Pattern 'Include `board_inputs/z20_v1_5_hardware_abi\.h` from C-side driver or HAL code after `scripts/verify_driver_contract\.ps1` passes' -Label 'driver contract header instruction'
Assert-TextContains -Text $text -Pattern 'Copy `board_inputs/z20_v1_5_axi_lite\.dtsi` into the Linux device-tree overlay or `system-user\.dtsi` after `scripts/verify_driver_contract\.ps1` passes' -Label 'driver contract DTSI instruction'
Assert-TextContains -Text $text -Pattern 'Keep `board_inputs/v3_hardware_abi\.h` only as the compatibility ABI header' -Label 'compatibility ABI header instruction'
Assert-TextContains -Text $text -Pattern 'Use `board_inputs/pl_estop_register_map\.md` as the PL E-stop AXI register map' -Label 'PL E-stop register map instruction'
Assert-TextContains -Text $text -Pattern 'Use `docs/io_owner_register_map\.md` as the Z20 v1\.5 IO owner AXI register map' -Label 'Z20 v1.5 IO owner register map instruction'
Assert-TextContains -Text $text -Pattern 'scripts/verify_pl_estop_register_map\.ps1' -Label 'PL E-stop register map verification instruction'
Assert-TextContains -Text $text -Pattern 'scripts/verify_pl_estop_timing_params\.ps1' -Label 'PL E-stop timing parameter verification instruction'
Assert-TextContains -Text $text -Pattern 'pl_estop_timing_params=ok' -Label 'PL E-stop timing parameter expected state'
Assert-TextContains -Text $text -Pattern 'scripts/verify_new_vivado_local_closure\.ps1' -Label 'local closure verification instruction'
Assert-TextContains -Text $text -Pattern 'code-review-only scope' -Label 'code-review-only local closure boundary'
Assert-TextContains -Text $text -Pattern 'new_vivado_local_closure=local_verified_only' -Label 'local closure expected state'
Assert-TextContains -Text $text -Pattern 'scripts/verify_project_portability\.ps1' -Label 'project portability verification instruction'
Assert-TextContains -Text $text -Pattern 'scripts/verify_adc_spi_mapping\.ps1' -Label 'ADC SPI verification instruction'
Assert-TextContains -Text $text -Pattern 'scripts/verify_no_legacy_axis_adc_boundary\.ps1' -Label 'legacy axis ADC boundary verification instruction'
Assert-TextContains -Text $text -Pattern 'axis_functional_completion=vivado_io_owner_connected' -Label 'axis IO owner connected expected state'
Assert-TextContains -Text $text -Pattern 'axis_ena_owner=z20_v15_io_owner_axi_lite' -Label 'axis ENA owner expected state'
Assert-TextContains -Text $text -Pattern 'do_pwm_normal_owner=z20_v15_io_owner_do_pwm' -Label 'DO/PWM owner expected state'
Assert-TextContains -Text $text -Pattern 'rs485_boundary=exported_ps_uart1_emio' -Label 'RS485 exported expected state'
Assert-TextContains -Text $text -Pattern 'touch_int_rst_boundary=z20_v15_io_owner_tp_int_rst' -Label 'touch owner expected state'
Assert-TextContains -Text $text -Pattern 'scripts/verify_z20_v15_io_owner_sim\.ps1' -Label 'Z20 IO owner simulation instruction'
Assert-TextContains -Text $text -Pattern 'z20_v15_io_owner_sim=ok' -Label 'Z20 IO owner simulation expected state'
Assert-TextContains -Text $text -Pattern 'scripts/verify_vivado_warning_summary\.ps1' -Label 'Vivado warning summary verification instruction'
Assert-TextContains -Text $text -Pattern 'constraint_truth_warning_lines=0' -Label 'constraint warning summary instruction'
Assert-TextContains -Text $text -Pattern 'unexpected_warning_codes=0' -Label 'unexpected warning summary instruction'
Assert-TextContains -Text $text -Pattern 'scripts/verify_pl_estop_real_pin_promotion_gate\.ps1' -Label 'real pin promotion gate instruction'
Assert-TextContains -Text $text -Pattern 'scripts/verify_pl_estop_bus_gate_owner\.ps1' -Label 'bus gate owner instruction'
Assert-TextContains -Text $text -Pattern 'adc_xadc_pins=L11,M12' -Label 'ADC XADC pin instruction'
Assert-TextContains -Text $text -Pattern 'Do not treat `system\.xsa` or `system_top\.bit` as board-verified safety behavior' -Label 'not board verified warning'
Assert-TextContains -Text $text -Pattern 'current plan is code review only and does not execute physical measurement' -Label 'code-only physical measurement boundary'
Assert-TextContains -Text $text -Pattern 'Use `docs/pl_estop_field_packet\.md` as the field intake packet' -Label 'field packet instruction'
Assert-TextContains -Text $text -Pattern 'Use `docs/pl_estop_field_execution_runbook\.md` as the step-by-step field sequence' -Label 'field runbook instruction'
Assert-TextContains -Text $text -Pattern 'Use `docs/pl_estop_evidence_record_templates\.md` to create real `\.md` evidence records' -Label 'evidence templates instruction'
Assert-TextContains -Text $text -Pattern 'do not reference the generated template file itself from CSV evidence fields' -Label 'template not evidence warning'
Assert-TextContains -Text $text -Pattern 'Store future PL E-stop bench and board proof files under `docs/evidence/pl_estop/`' -Label 'evidence root instruction'
Assert-TextContains -Text $text -Pattern 'Do not use `scripts/verify_pl_estop_field_intake\.ps1` to promote physical measurement scope in this plan' -Label 'field intake out-of-scope instruction'
Assert-TextContains -Text $text -Pattern 'Real E-stop input: active XDC top input, local hard-gate only, board validation not run' -Label 'E-stop input boundary'
Assert-TextContains -Text $text -Pattern 'Axis interface: wrapper external boundary is current 8-bit axis naming; PULS/DIR/ABZ are owned by `step_ip`, and ENA1-8 are owned by `z20_v15_io_owner_axi_lite` before the top E-stop gate' -Label 'axis functional boundary'
Assert-TextContains -Text $text -Pattern 'DI/FR_DI/TS_DI/MPG/SCALE/ALM: top pins feed `z20_v15_io_owner_axi_lite` input synchronizers/status registers for code-review-level IO closure' -Label 'input owner boundary'
Assert-TextContains -Text $text -Pattern 'RS485: PS UART1 EMIO is exported to `RS485_FPGA_RX`/`RS485_FPGA_TX` and constrained from the v1.5 source XDC' -Label 'RS485 exported boundary'
Assert-TextContains -Text $text -Pattern 'TP_INT/TP_RST: `TP_INT` feeds the IO owner status path, and `TP_RST` is driven by the IO owner touch reset output defaulting released high' -Label 'touch INT/RST boundary'
Assert-TextContains -Text $text -Pattern 'Real DO/PWM outputs: active XDC top outputs, forced-off local build, board validation not run' -Label 'DO/PWM boundary'
Assert-TextContains -Text $text -Pattern 'Real bus TX or driver-enable gate: PS GEM1/EMIO GMII TX_EN/TX_ER/TXD is locally gated before gmii2rgmii; RGMII Link/RX/MDIO/clock are preserved by design, board validation not run' -Label 'bus TX boundary'
Assert-TextContains -Text $text -Pattern 'Board safety validation: not run' -Label 'board validation boundary'

if ($timingParams.pl_estop_timing_params -ne 'ok' -or
    $timingParams.clock_hz -ne '100000000' -or
    $timingParams.debounce_ms -ne '10' -or
    $timingParams.debounce_cycles -ne '1000000' -or
    $timingParams.brake_lead_us -ne '50' -or
    $timingParams.brake_cycles -ne '5000' -or
    $timingParams.axis_count -ne '8' -or
    $timingParams.z_axis_index -ne '2') {
  throw 'Board-input handoff PL E-stop timing parameter state is not closed'
}

Write-Output 'board_input_handoff_verify=ok'
Write-Output "board_input_handoff_readme=$handoffRel"
Write-Output 'board_input_handoff_software_handoff=board_inputs/software_handoff.md'
Write-Output 'board_input_handoff_hardware_abi_header=board_inputs/v3_hardware_abi.h'
Write-Output "board_input_handoff_hardware_abi_header_verify=$($abiHeader.hardware_abi_header)"
Write-Output 'board_input_handoff_driver_contract_header=board_inputs/z20_v1_5_hardware_abi.h'
Write-Output 'board_input_handoff_driver_contract_dtsi=board_inputs/z20_v1_5_axi_lite.dtsi'
Write-Output "board_input_handoff_driver_contract_verify=$($driverContract.driver_contract)"
Write-Output 'board_input_handoff_register_map=board_inputs/pl_estop_register_map.md'
Write-Output 'board_input_handoff_io_owner_register_map=docs/io_owner_register_map.md'
Write-Output "board_input_handoff_register_map_verify=$($registerMap.pl_estop_register_map)"
Write-Output 'board_input_handoff_state=local_verified_only'
Write-Output "board_input_handoff_active_xdc_traceability=$($traceability.active_xdc_traceability)"
Write-Output "board_input_handoff_active_xdc_electrical_contract=$($electrical.active_xdc_electrical_contract)"
Write-Output "board_input_handoff_vivado_xsa_cleanliness=$($vivadoCleanliness.vivado_xsa_cleanliness)"
Write-Output "board_input_handoff_vivado_warning_summary=$($warningSummary.vivado_warning_summary)"
Write-Output "board_input_handoff_vivado_warning_summary_verify=$($warningSummaryVerify.vivado_warning_summary_verify)"
Write-Output "board_input_handoff_legacy_axis_adc_boundary=$($legacyBoundary.legacy_axis_adc_boundary)"
Write-Output "board_input_handoff_wrapper_axis_boundary=$($legacyBoundary.wrapper_axis_boundary)"
Write-Output "board_input_handoff_axis_functional_completion=$($legacyBoundary.axis_functional_completion)"
Write-Output "board_input_handoff_z20_v15_io_owner_sim=$($ioOwnerSim.z20_v15_io_owner_sim)"
Write-Output "board_input_handoff_readiness=$($readiness.pl_estop_readiness)"
Write-Output "board_input_handoff_pl_estop_sim=$($sim.pl_estop_sim)"
Write-Output "board_input_handoff_timing_params=$($timingParams.pl_estop_timing_params)"
Write-Output "board_input_handoff_request_verify=$($hardwareRequest.pl_estop_hardware_evidence_request_verify)"
Write-Output "board_input_handoff_field_packet_verify=$($fieldPacket.pl_estop_field_packet_verify)"
Write-Output "board_input_handoff_field_runbook_verify=$($fieldRunbook.pl_estop_field_runbook_verify)"
Write-Output "board_input_handoff_evidence_templates_verify=$($evidenceTemplates.pl_estop_evidence_templates_verify)"
Write-Output "board_input_handoff_evidence_root_verify=$($evidenceRoot.pl_estop_evidence_root_verify)"
Write-Output "board_input_handoff_field_intake=$($fieldIntake.pl_estop_field_intake)"
Write-Output "board_input_handoff_field_intake_contract=$($fieldIntake.field_intake_structural_contract)"
Write-Output "board_input_handoff_real_pin_promotion_gate=$($realPinPromotion.pl_estop_real_pin_promotion_gate)"
Write-Output "board_input_handoff_output_shutdown_contract=$($outputShutdown.pl_estop_output_shutdown_contract)"
Write-Output "board_input_handoff_bus_gate_owner=$($busGateOwner.pl_estop_bus_gate_owner)"
Write-Output "board_input_handoff_bus_gate_board_evidence=$($busGateOwner.board_evidence_state)"
