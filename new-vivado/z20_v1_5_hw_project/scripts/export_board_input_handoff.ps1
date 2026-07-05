param(
  [string]$ProjectDir,
  [string]$OutPath
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($OutPath)) {
  $OutPath = Join-Path $ProjectDir 'board_inputs/README.md'
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

function Get-ProjectRelativeOutputPath {
  param(
    [string]$RootDir,
    [string]$Path
  )

  if (Test-Path -LiteralPath $Path) {
    return Convert-ToProjectRelativePath -RootDir $RootDir -Path $Path
  }
  $root = (Resolve-Path -LiteralPath $RootDir).Path.TrimEnd('\', '/')
  $parent = Split-Path -Parent $Path
  $fullParent = (Resolve-Path -LiteralPath $parent).Path
  $candidate = Join-Path $fullParent (Split-Path -Leaf $Path)
  return (($candidate.Substring($root.Length).TrimStart('\', '/')) -replace '\\', '/')
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

function Get-ArtifactRow {
  param(
    [string]$RootDir,
    [string]$RelativePath,
    [string]$Label
  )

  $path = Join-Path $RootDir ($RelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing handoff artifact: $RelativePath"
  }
  $item = Get-Item -LiteralPath $path
  $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
  return "| $Label | ``$($RelativePath -replace '\\', '/')`` | $($item.Length) | ``$hash`` |"
}

$outDir = Split-Path -Parent $OutPath
if (-not (Test-Path -LiteralPath $outDir)) {
  New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

$timingHistoryPath = Join-Path $ProjectDir 'artifacts/vivado/timing_history.csv'
if (-not (Test-Path -LiteralPath $timingHistoryPath)) {
  throw 'Missing artifacts/vivado/timing_history.csv'
}
$timingRows = @(Import-Csv -LiteralPath $timingHistoryPath)
if ($timingRows.Count -eq 0) {
  throw 'Timing history has no rows'
}
$latestTiming = $timingRows[-1]

$traceability = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_active_xdc_traceability.ps1'
$electrical = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_active_xdc_electrical_contract.ps1'
$vivadoCleanliness = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_vivado_xsa_cleanliness.ps1'
$abiHeader = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/export_hardware_abi_header.ps1'
$driverContract = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/export_driver_contract.ps1'
$warningSummary = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/export_vivado_warning_summary.ps1'
$warningSummaryVerify = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_vivado_warning_summary.ps1'
$readiness = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_readiness.ps1'
$portability = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_project_portability.ps1'
$adc = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_adc_spi_mapping.ps1'
$legacyBoundary = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_no_legacy_axis_adc_boundary.ps1'
$ioOwnerSim = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_z20_v15_io_owner_sim.ps1'
$sim = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_sim.ps1'
$timingParams = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_timing_params.ps1'
$hardwareRequest = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_hardware_evidence_request.ps1'
$fieldPacketExport = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/export_pl_estop_field_packet.ps1'
$fieldPacketVerify = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_field_packet.ps1'
$fieldRunbook = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_field_runbook.ps1'
$evidenceTemplatesExport = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/export_pl_estop_evidence_templates.ps1'
$evidenceTemplatesVerify = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_evidence_templates.ps1'
$evidenceRoot = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_evidence_root.ps1'
$fieldIntake = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_field_intake.ps1'
$realPinPromotion = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_real_pin_promotion_gate.ps1'
$outputShutdown = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_output_shutdown_contract.ps1'
$busGateOwner = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_bus_gate_owner.ps1'
$registerMap = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_register_map.ps1'
$safety = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_safety_boundary.ps1'
$outRel = Get-ProjectRelativeOutputPath -RootDir $ProjectDir -Path $OutPath

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# Z20 v1.5 Board Input Handoff')
$lines.Add('')
$lines.Add('This generated handoff is for the local Vivado board-input artifacts in this directory. It is not board safety proof.')
$lines.Add('')
$lines.Add('## Current State')
$lines.Add('')
$lines.Add('- Board closure state: `local_verified_only`')
$lines.Add("- Active XDC traceability: ``$($traceability.active_xdc_traceability)``")
$lines.Add("- Active XDC traced assignments: ``$($traceability.traced_assignments)``")
$lines.Add("- Active XDC electrical contract: ``$($electrical.active_xdc_electrical_contract)``")
$lines.Add("- Active XDC IOSTANDARD assignments: ``$($electrical.iostandard_assignments)``")
$lines.Add("- Active XDC LVCMOS33 assignments: ``$($electrical.lvcmos33_assignments)``")
$lines.Add("- Vivado/XSA cleanliness: ``$($vivadoCleanliness.vivado_xsa_cleanliness)``")
$lines.Add("- Active constraints loaded: ``$($vivadoCleanliness.active_constraints_loaded)``")
$lines.Add("- Truth/source XDC loaded by Vivado: ``$($vivadoCleanliness.truth_source_xdc_loaded)``")
$lines.Add("- Old project XDC loaded by Vivado: ``$($vivadoCleanliness.old_project_xdc_loaded)``")
$lines.Add("- DRC blocking rules: ``$($vivadoCleanliness.drc_blocking_rules)``")
$lines.Add("- DRC allowed warning rules: ``$($vivadoCleanliness.drc_allowed_warning_rules)``")
$lines.Add("- Hardware ABI header: ``$($abiHeader.hardware_abi_header)``")
$lines.Add("- Hardware ABI header output: ``$($abiHeader.output)``")
$lines.Add("- Hardware ABI address anchors: step_ip ``$($abiHeader.step_ip_base)`` / pl_estop ``$($abiHeader.pl_estop_base)`` / io_owner ``$($abiHeader.io_owner_base)``")
$lines.Add("- Driver contract: ``$($driverContract.driver_contract)``")
$lines.Add("- Driver contract header: ``$($driverContract.header)``")
$lines.Add("- Driver contract DTSI: ``$($driverContract.dtsi)``")
$lines.Add("- PL E-stop DTS interrupt: IRQ_F2P[``$($driverContract.pl_estop_irq_f2p_bit)``] / GIC SPI cell ``$($driverContract.pl_estop_irq_gic_spi_cell)``")
$lines.Add("- Vivado warning summary: ``$($warningSummary.vivado_warning_summary)``")
$lines.Add("- Vivado warning summary verify: ``$($warningSummaryVerify.vivado_warning_summary_verify)``")
$lines.Add("- Vivado warning lines: ``$($warningSummary.vivado_warning_lines)``")
$lines.Add("- Vivado warning codes: ``$($warningSummary.vivado_warning_codes)``")
$lines.Add("- Constraint/source warning lines: ``$($warningSummary.constraint_truth_warning_lines)``")
$lines.Add("- Retired HDMI warning lines: ``$($warningSummary.retired_hdmi_warning_lines)``")
$lines.Add("- Project portability: ``$($portability.project_portability)``")
$lines.Add("- Absolute path scan: ``$($portability.absolute_path_scan)``")
$lines.Add("- Manifest relative paths: ``$($portability.manifest_relative_paths)``")
$lines.Add("- ADC mapping: ``$($adc.adc_mapping)``")
$lines.Add("- ADC owner: ``$($adc.adc_owner)``")
$lines.Add("- ADC XADC pins: ``$($adc.adc_xadc_pins)``")
$lines.Add("- ADC SPI mapping: ``$($adc.adc_spi_mapping)``")
$lines.Add("- Legacy axis/ADC external boundary: ``$($legacyBoundary.legacy_axis_adc_boundary)``")
$lines.Add("- Wrapper axis boundary: ``$($legacyBoundary.wrapper_axis_boundary)``")
$lines.Add("- Axis functional completion: ``$($legacyBoundary.axis_functional_completion)``")
$lines.Add("- Axis motion owner: ``$($legacyBoundary.axis_motion_owner)``")
$lines.Add("- Axis ENA owner: ``$($legacyBoundary.axis_ena_owner)``")
$lines.Add("- Axis 7/8 encoder processing: ``$($legacyBoundary.axis_78_encoder_processing)``")
$lines.Add("- DI/MPG/ALM processing: ``$($legacyBoundary.di_mpg_alarm_processing)``")
$lines.Add("- DO/PWM normal owner: ``$($legacyBoundary.do_pwm_normal_owner)``")
$lines.Add("- RS485 boundary: ``$($legacyBoundary.rs485_boundary)``")
$lines.Add("- Touch INT/RST boundary: ``$($legacyBoundary.touch_int_rst_boundary)``")
$lines.Add("- Z20 v1.5 IO owner simulation: ``$($ioOwnerSim.z20_v15_io_owner_sim)``")
$lines.Add("- PL E-stop simulation: ``$($sim.pl_estop_sim)``")
$lines.Add("- PL E-stop core testbench: ``$($sim.pl_estop_core_tb)``")
$lines.Add("- PL E-stop AXI testbench: ``$($sim.pl_estop_axi_lite_tb)``")
$lines.Add("- PL E-stop timing params: ``$($timingParams.pl_estop_timing_params)``")
$lines.Add("- PL E-stop timing clock/debounce/brake: ``$($timingParams.clock_hz)`` / ``$($timingParams.debounce_ms)`` ms / ``$($timingParams.brake_lead_us)`` us")
$lines.Add("- PL E-stop derived cycles: ``$($timingParams.debounce_cycles)`` debounce / ``$($timingParams.brake_cycles)`` brake")
$lines.Add("- PL E-stop axis config: ``$($timingParams.axis_count)`` axes / Z index ``$($timingParams.z_axis_index)``")
$lines.Add("- PL E-stop readiness: ``$($readiness.pl_estop_readiness)``")
$lines.Add("- Readiness blockers: ``$($readiness.readiness_blockers)``")
$lines.Add("- Hardware evidence request verify: ``$($hardwareRequest.pl_estop_hardware_evidence_request_verify)``")
$lines.Add("- Hardware evidence request state: ``$($hardwareRequest.hardware_evidence_request_state)``")
$lines.Add("- Field evidence packet verify: ``$($fieldPacketVerify.pl_estop_field_packet_verify)``")
$lines.Add("- Field evidence packet state: ``$($fieldPacketExport.pl_estop_field_packet)``")
$lines.Add("- Field execution runbook verify: ``$($fieldRunbook.pl_estop_field_runbook_verify)``")
$lines.Add("- Field execution runbook state: ``$($fieldRunbook.field_runbook_state)``")
$lines.Add("- Evidence record templates verify: ``$($evidenceTemplatesVerify.pl_estop_evidence_templates_verify)``")
$lines.Add("- Evidence record templates state: ``$($evidenceTemplatesExport.pl_estop_evidence_templates)``")
$lines.Add("- Evidence root verify: ``$($evidenceRoot.pl_estop_evidence_root_verify)``")
$lines.Add("- Orphan board-verified records: ``$($evidenceRoot.orphan_board_verified_records)``")
$lines.Add("- Field intake gate: ``$($fieldIntake.pl_estop_field_intake)``")
$lines.Add("- Field intake structural contract: ``$($fieldIntake.field_intake_structural_contract)``")
$lines.Add("- Field intake blockers: ``$($fieldIntake.field_intake_blockers)``")
$lines.Add("- Real pin promotion gate: ``$($realPinPromotion.pl_estop_real_pin_promotion_gate)``")
$lines.Add("- Output shutdown contract: ``$($outputShutdown.pl_estop_output_shutdown_contract)``")
$lines.Add("- Output shutdown DO/PWM rows: ``$($outputShutdown.do_pwm_wiring_rows)``")
$lines.Add("- Output shutdown bus TX rows: ``$($outputShutdown.bus_tx_wiring_rows)``")
$lines.Add("- Bus gate owner: ``$($busGateOwner.pl_estop_bus_gate_owner)``")
$lines.Add("- Bus gate transport: ``$($busGateOwner.production_transport)``")
$lines.Add("- Bus gate board evidence: ``$($busGateOwner.board_evidence_state)``")
$lines.Add("- PL E-stop register map: ``$($registerMap.pl_estop_register_map)``")
$lines.Add("- PL E-stop register count: ``$($registerMap.registers)``")
$lines.Add("- PL E-stop status/control bits: ``$($registerMap.status_bits)`` / ``$($registerMap.control_bits)``")
$lines.Add("- Active promoted wiring assignments: ``$($realPinPromotion.active_promoted_wiring_assignments)``")
$lines.Add("- Active promoted DO/PWM assignments: ``$($realPinPromotion.active_promoted_do_pwm_assignments)``")
$lines.Add("- Active promoted bus TX assignments: ``$($realPinPromotion.active_promoted_bus_tx_assignments)``")
$lines.Add("- Promotion requires E11: ``$($realPinPromotion.promotion_requires_e11)``")
$lines.Add("- PL E-stop safety boundary: ``$($safety.pl_estop_safety_boundary)``")
$lines.Add("- Active pending safety-pin assignments: ``$($safety.active_pending_wiring_pin_assignments)``")
$lines.Add('')
$lines.Add('## Timing Snapshot')
$lines.Add('')
$lines.Add("- Timestamp: ``$($latestTiming.timestamp)``")
$lines.Add("- Build status: ``$($latestTiming.build_status)``")
$lines.Add("- Timing status: ``$($latestTiming.timing_status)``")
$lines.Add("- WNS/WHS: ``$($latestTiming.wns)`` / ``$($latestTiming.whs)``")
$lines.Add("- Bit file: ``$($latestTiming.bit_file)``")
$lines.Add('')
$lines.Add('## Artifacts')
$lines.Add('')
$lines.Add('| Item | Project-relative path | Bytes | SHA256 |')
$lines.Add('| --- | --- | ---: | --- |')
$lines.Add((Get-ArtifactRow -RootDir $ProjectDir -RelativePath 'board_inputs/system.xsa' -Label 'XSA with bitstream'))
$lines.Add((Get-ArtifactRow -RootDir $ProjectDir -RelativePath 'board_inputs/software_handoff.md' -Label 'Board software handoff'))
$lines.Add((Get-ArtifactRow -RootDir $ProjectDir -RelativePath 'board_inputs/z20_v1_5_hardware_abi.h' -Label 'Driver contract C header'))
$lines.Add((Get-ArtifactRow -RootDir $ProjectDir -RelativePath 'board_inputs/z20_v1_5_axi_lite.dtsi' -Label 'Driver contract DTSI'))
$lines.Add((Get-ArtifactRow -RootDir $ProjectDir -RelativePath 'board_inputs/v3_hardware_abi.h' -Label 'Compatibility C hardware ABI header'))
$lines.Add((Get-ArtifactRow -RootDir $ProjectDir -RelativePath 'board_inputs/pl_estop_register_map.md' -Label 'PL E-stop register map'))
$lines.Add((Get-ArtifactRow -RootDir $ProjectDir -RelativePath 'docs/io_owner_register_map.md' -Label 'Z20 v1.5 IO owner register map'))
$lines.Add((Get-ArtifactRow -RootDir $ProjectDir -RelativePath 'z20_v1_5_hw_project.runs/impl_1/system_top.bit' -Label 'Bitstream'))
$lines.Add((Get-ArtifactRow -RootDir $ProjectDir -RelativePath 'constraints/z20_v1_5_active_mapped.xdc' -Label 'Active XDC'))
$lines.Add((Get-ArtifactRow -RootDir $ProjectDir -RelativePath 'docs/pl_estop_hardware_evidence_request.md' -Label 'Hardware evidence request'))
$lines.Add((Get-ArtifactRow -RootDir $ProjectDir -RelativePath 'docs/pl_estop_field_packet.md' -Label 'Field evidence packet'))
$lines.Add((Get-ArtifactRow -RootDir $ProjectDir -RelativePath 'docs/pl_estop_field_execution_runbook.md' -Label 'Field execution runbook'))
$lines.Add((Get-ArtifactRow -RootDir $ProjectDir -RelativePath 'docs/pl_estop_evidence_record_templates.md' -Label 'Evidence record templates'))
$lines.Add((Get-ArtifactRow -RootDir $ProjectDir -RelativePath 'docs/vivado_warning_summary.csv' -Label 'Vivado warning summary CSV'))
$lines.Add((Get-ArtifactRow -RootDir $ProjectDir -RelativePath 'docs/vivado_warning_summary.md' -Label 'Vivado warning summary report'))
$lines.Add((Get-ArtifactRow -RootDir $ProjectDir -RelativePath 'docs/pl_estop_evidence_gap.md' -Label 'Evidence gap report'))
$lines.Add((Get-ArtifactRow -RootDir $ProjectDir -RelativePath 'docs/evidence/pl_estop/README.md' -Label 'Evidence-file root policy'))
$lines.Add('')
$lines.Add('## Before Board Use')
$lines.Add('')
$lines.Add('- Run `scripts/verify_board_input_manifest.ps1` from the project directory and require `hashes=ok`.')
$lines.Add('- Use `board_inputs/software_handoff.md` as the board-software-facing summary; it stays inside the Vivado project and contains only project-relative paths.')
$lines.Add('- Include `board_inputs/z20_v1_5_hardware_abi.h` from C-side driver or HAL code after `scripts/verify_driver_contract.ps1` passes; do not hand-copy AXI bases or register offsets.')
$lines.Add('- Copy `board_inputs/z20_v1_5_axi_lite.dtsi` into the Linux device-tree overlay or `system-user.dtsi` after `scripts/verify_driver_contract.ps1` passes.')
$lines.Add('- Keep `board_inputs/v3_hardware_abi.h` only as the compatibility ABI header generated from the same XSA/register-map source.')
$lines.Add('- Use `board_inputs/pl_estop_register_map.md` as the PL E-stop AXI register map; run `scripts/verify_pl_estop_register_map.ps1` before wiring board software to the block.')
$lines.Add('- Use `docs/io_owner_register_map.md` as the Z20 v1.5 IO owner AXI register map before wiring LinuxCNC/HAL to DI, FR_DI, MPG, ALM, DO, PWM, ENA, TP_INT, or TP_RST.')
$lines.Add('- Run `scripts/verify_pl_estop_timing_params.ps1` before changing PL E-stop debounce, brake, axis count, or AXI clock assumptions; current expected state is `pl_estop_timing_params=ok` at 100 MHz.')
$lines.Add('- Run `scripts/verify_new_vivado_local_closure.ps1` as the local handoff gate; require `new_vivado_local_closure=local_verified_only` under the current code-review-only scope.')
$lines.Add('- Run `scripts/verify_project_portability.ps1` before moving or handing off the folder; require `project_portability=ok` and `absolute_path_scan=ok`.')
$lines.Add('- Run `scripts/verify_adc_spi_mapping.ps1` before any ADC-related handoff; require `adc_mapping=ok`, `adc_xadc_pins=L11,M12`, `adc_spi_mapping=retired`, and `active_adc_spi_assignments=0`.')
$lines.Add('- Run `scripts/verify_no_legacy_axis_adc_boundary.ps1` before any wrapper regeneration handoff; require `legacy_axis_adc_boundary=retired`, `wrapper_axis_boundary=current_8bit`, `axis_functional_completion=vivado_io_owner_connected`, `axis_ena_owner=z20_v15_io_owner_axi_lite`, `do_pwm_normal_owner=z20_v15_io_owner_do_pwm`, `rs485_boundary=exported_ps_uart1_emio`, and `touch_int_rst_boundary=z20_v15_io_owner_tp_int_rst`.')
$lines.Add('- Run `scripts/verify_z20_v15_io_owner_sim.ps1` before treating DO/PWM/ENA/input/touch owner RTL as locally closed; require `z20_v15_io_owner_sim=ok`.')
$lines.Add('- Run `scripts/verify_vivado_warning_summary.ps1` before treating the XSA log state as clean; require `constraint_truth_warning_lines=0` and `unexpected_warning_codes=0`.')
$lines.Add('- Run `scripts/verify_pl_estop_real_pin_promotion_gate.ps1` before and after any real E-stop, STO, brake, DO/PWM, axis, or bus TX active-XDC promotion; current expected state is `local_hard_gate_promoted` for the E-stop input plus 16 DO/PWM outputs.')
$lines.Add('- Run `scripts/verify_pl_estop_bus_gate_owner.ps1` before board handoff; current expected state is `pl_estop_bus_gate_owner=ps_gem1_emio_rgmii_local_verified` and `board_evidence_state=pending` because physical measurement is outside this plan.')
$lines.Add('- Do not treat `system.xsa` or `system_top.bit` as board-verified safety behavior.')
$lines.Add('- Do not claim real E-stop, DO/PWM, or bus TX safety behavior from this handoff; the current plan is code review only and does not execute physical measurement.')
$lines.Add('- Do not promote remaining STO, brake, axis, or any additional bus TX owner in this plan; keep unreviewed external safety outputs out of scope.')
$lines.Add('- Treat `docs/pl_estop_hardware_evidence_request.md` as reference material only under the current code-review-only plan.')
$lines.Add('- Use `docs/pl_estop_field_packet.md` as the field intake packet for exact CSV fields and suggested evidence file names.')
$lines.Add('- Use `docs/pl_estop_field_execution_runbook.md` as the step-by-step field sequence before changing real RTL/XDC wiring.')
$lines.Add('- Use `docs/pl_estop_evidence_record_templates.md` to create real `.md` evidence records under `docs/evidence/pl_estop/`; do not reference the generated template file itself from CSV evidence fields.')
$lines.Add('- Store future PL E-stop bench and board proof files under `docs/evidence/pl_estop/`; verified CSV rows must reference files under that directory.')
$lines.Add('- Do not use `scripts/verify_pl_estop_field_intake.ps1` to promote physical measurement scope in this plan.')
$lines.Add('')
$lines.Add('## Safety Boundaries')
$lines.Add('')
$lines.Add('- Real E-stop input: active XDC top input, local hard-gate only, board validation not run.')
$lines.Add('- Axis interface: wrapper external boundary is current 8-bit axis naming; PULS/DIR/ABZ are owned by `step_ip`, and ENA1-8 are owned by `z20_v15_io_owner_axi_lite` before the top E-stop gate. Board motion validation is not run.')
$lines.Add('- DI/FR_DI/TS_DI/MPG/SCALE/ALM: top pins feed `z20_v15_io_owner_axi_lite` input synchronizers/status registers for code-review-level IO closure. Board input validation is not run.')
$lines.Add('- RS485: PS UART1 EMIO is exported to `RS485_FPGA_RX`/`RS485_FPGA_TX` and constrained from the v1.5 source XDC. Board serial validation is not run.')
$lines.Add('- TP_INT/TP_RST: `TP_INT` feeds the IO owner status path, and `TP_RST` is driven by the IO owner touch reset output defaulting released high. Board touch validation is not run.')
$lines.Add('- Real STO, brake, and axis outputs: not connected.')
$lines.Add('- Real DO/PWM outputs: active XDC top outputs, forced-off local build, board validation not run.')
$lines.Add('- Real bus TX or driver-enable gate: PS GEM1/EMIO GMII TX_EN/TX_ER/TXD is locally gated before gmii2rgmii; RGMII Link/RX/MDIO/clock are preserved by design, board validation not run.')
$lines.Add('- Board deploy: not run.')
$lines.Add('- Board safety validation: not run.')

$text = ($lines -join [Environment]::NewLine) + [Environment]::NewLine
if ($text -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
  throw 'Generated board-input handoff contains an absolute path'
}
if ($text -match 'vivado_hw_project') {
  throw 'Generated board-input handoff contains an old-project reference'
}

Write-TextFileWithRetry -Path $OutPath -Text $text

Write-Output 'board_input_handoff=local_verified_only'
Write-Output "board_input_handoff_readme=$outRel"
Write-Output 'board_input_handoff_software_handoff=board_inputs/software_handoff.md'
Write-Output 'board_input_handoff_hardware_abi_header=board_inputs/v3_hardware_abi.h'
Write-Output "board_input_handoff_hardware_abi_header_verify=$($abiHeader.hardware_abi_header)"
Write-Output 'board_input_handoff_driver_contract_header=board_inputs/z20_v1_5_hardware_abi.h'
Write-Output 'board_input_handoff_driver_contract_dtsi=board_inputs/z20_v1_5_axi_lite.dtsi'
Write-Output "board_input_handoff_driver_contract_verify=$($driverContract.driver_contract)"
Write-Output 'board_input_handoff_register_map=board_inputs/pl_estop_register_map.md'
Write-Output 'board_input_handoff_io_owner_register_map=docs/io_owner_register_map.md'
Write-Output "board_input_handoff_register_map_verify=$($registerMap.pl_estop_register_map)"
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
Write-Output "board_input_handoff_field_packet_verify=$($fieldPacketVerify.pl_estop_field_packet_verify)"
Write-Output "board_input_handoff_field_runbook_verify=$($fieldRunbook.pl_estop_field_runbook_verify)"
Write-Output "board_input_handoff_evidence_templates_verify=$($evidenceTemplatesVerify.pl_estop_evidence_templates_verify)"
Write-Output "board_input_handoff_evidence_root_verify=$($evidenceRoot.pl_estop_evidence_root_verify)"
Write-Output "board_input_handoff_field_intake=$($fieldIntake.pl_estop_field_intake)"
Write-Output "board_input_handoff_field_intake_contract=$($fieldIntake.field_intake_structural_contract)"
Write-Output "board_input_handoff_real_pin_promotion_gate=$($realPinPromotion.pl_estop_real_pin_promotion_gate)"
Write-Output "board_input_handoff_output_shutdown_contract=$($outputShutdown.pl_estop_output_shutdown_contract)"
