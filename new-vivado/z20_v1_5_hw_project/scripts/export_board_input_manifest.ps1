param(
  [string]$ProjectDir,
  [string]$OutPath
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($OutPath)) {
  $OutPath = Join-Path $ProjectDir 'board_inputs/manifest.json'
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

function Get-ArtifactInfo {
  param(
    [string]$RootDir,
    [string]$RelativePath
  )

  $fullPath = Join-Path $RootDir ($RelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
  if (-not (Test-Path -LiteralPath $fullPath)) {
    throw "Missing artifact: $RelativePath"
  }
  $item = Get-Item -LiteralPath $fullPath
  $hash = Get-FileHash -LiteralPath $fullPath -Algorithm SHA256
  return [ordered]@{
    path = ($RelativePath -replace '\\', '/')
    bytes = $item.Length
    last_write_time_utc = $item.LastWriteTimeUtc.ToString('o')
    sha256 = $hash.Hash.ToLowerInvariant()
  }
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
    [string]$ProjectDir,
    [string]$ScriptRelativePath
  )

  $scriptPath = Join-Path $ProjectDir ($ScriptRelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
  if (-not (Test-Path -LiteralPath $scriptPath)) {
    throw "Missing script: $ScriptRelativePath"
  }
  $output = @(& powershell -NoProfile -ExecutionPolicy Bypass -File $scriptPath -ProjectDir $ProjectDir)
  if ($LASTEXITCODE -ne 0) {
    throw "$ScriptRelativePath failed: $($output -join '; ')"
  }
  return [ordered]@{
    script = $ScriptRelativePath
    output = $output
    parsed = Convert-KeyValueOutput -Lines $output
  }
}

$projectPath = Join-Path $ProjectDir 'z20_v1_5_hw_project.xpr'
$timingHistoryPath = Join-Path $ProjectDir 'artifacts/vivado/timing_history.csv'
$outDir = Split-Path -Parent $OutPath
if (-not (Test-Path -LiteralPath $outDir)) {
  New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

foreach ($required in @($projectPath, $timingHistoryPath)) {
  if (-not (Test-Path -LiteralPath $required)) {
    throw "Missing required file: $(Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $required)"
  }
}

$timingRows = @(Import-Csv -LiteralPath $timingHistoryPath)
if ($timingRows.Count -eq 0) {
  throw 'Timing history has no rows'
}
$latestTiming = $timingRows[-1]

$checks = [ordered]@{
  check_active_xdc = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/check_active_xdc.ps1'
  active_xdc_traceability = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_active_xdc_traceability.ps1'
  active_xdc_electrical_contract = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_active_xdc_electrical_contract.ps1'
  vivado_xsa_cleanliness = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_vivado_xsa_cleanliness.ps1'
  hardware_abi_header = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/export_hardware_abi_header.ps1'
  hardware_abi_header_verify = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_hardware_abi_header.ps1'
  driver_contract = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/export_driver_contract.ps1'
  driver_contract_verify = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_driver_contract.ps1'
  vivado_warning_summary = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/export_vivado_warning_summary.ps1'
  vivado_warning_summary_verify = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_vivado_warning_summary.ps1'
  project_independence = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_project_independence.ps1'
  project_portability = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_project_portability.ps1'
  adc_mapping = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_adc_spi_mapping.ps1'
  legacy_axis_adc_boundary = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_no_legacy_axis_adc_boundary.ps1'
  z20_v15_io_owner_sim = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_z20_v15_io_owner_sim.ps1'
  pl_estop_sim = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_sim.ps1'
  pl_estop_timing_params = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_timing_params.ps1'
  pl_estop_safety_boundary = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_safety_boundary.ps1'
  pl_estop_output_shutdown_contract = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_output_shutdown_contract.ps1'
  pl_estop_bus_gate_owner = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_bus_gate_owner.ps1'
  pl_estop_register_map = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_register_map.ps1'
  pl_estop_wiring_evidence = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_wiring_evidence.ps1'
  pl_estop_board_validation = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_board_validation.ps1'
  pl_estop_evidence_gap = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/export_pl_estop_evidence_gap.ps1'
  pl_estop_hardware_evidence_request = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/export_pl_estop_hardware_evidence_request.ps1'
  pl_estop_hardware_evidence_request_verify = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_hardware_evidence_request.ps1'
  pl_estop_field_packet = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/export_pl_estop_field_packet.ps1'
  pl_estop_field_packet_verify = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_field_packet.ps1'
  pl_estop_field_runbook = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_field_runbook.ps1'
  pl_estop_evidence_templates = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/export_pl_estop_evidence_templates.ps1'
  pl_estop_evidence_templates_verify = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_evidence_templates.ps1'
  pl_estop_evidence_root = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_evidence_root.ps1'
  pl_estop_field_intake = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_field_intake.ps1'
  pl_estop_real_pin_promotion_gate = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_real_pin_promotion_gate.ps1'
  pl_estop_readiness = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_readiness.ps1'
  board_input_handoff = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/export_board_input_handoff.ps1'
  board_input_handoff_verify = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_board_input_handoff.ps1'
  verify_remaining_drc_ports = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/verify_remaining_drc_ports.ps1'
  export_active_pin_conflicts = Invoke-ProjectScript -ProjectDir $ProjectDir -ScriptRelativePath 'scripts/export_active_pin_conflicts.ps1'
}

$artifacts = @(
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'z20_v1_5_hw_project.runs/impl_1/system_top.bit'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'board_inputs/system.xsa'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'constraints/z20_v1_5_active_mapped.xdc'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'docs/pl_estop_wiring_evidence.csv'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'docs/pl_estop_board_validation_evidence.csv'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'docs/pl_estop_evidence_gap.md'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'docs/pl_estop_hardware_evidence_request.md'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'docs/pl_estop_field_packet.md'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'docs/pl_estop_field_execution_runbook.md'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'docs/pl_estop_evidence_record_templates.md'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'docs/vivado_warning_summary.csv'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'docs/vivado_warning_summary.md'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'docs/evidence/pl_estop/README.md'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'board_inputs/README.md'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'board_inputs/software_handoff.md'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'board_inputs/v3_hardware_abi.h'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'board_inputs/z20_v1_5_hardware_abi.h'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'board_inputs/z20_v1_5_axi_lite.dtsi'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'board_inputs/pl_estop_register_map.md'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'docs/io_owner_register_map.md'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'z20_v1_5_hw_project.xpr'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'z20_v1_5_hw_project.runs/impl_1/system_top_drc_routed.rpt'
  Get-ArtifactInfo -RootDir $ProjectDir -RelativePath 'artifacts/vivado/timing_history.csv'
)

$manifest = [ordered]@{
  schema = 'z20_v1_5_board_input_manifest.v1'
  generated_at_utc = (Get-Date).ToUniversalTime().ToString('o')
  project = 'z20_v1_5_hw_project'
  board_closure_state = 'local_verified_only'
  artifact_paths_are_relative_to = 'new-vivado/z20_v1_5_hw_project'
  timing_latest = [ordered]@{
    timestamp = $latestTiming.timestamp
    build_status = $latestTiming.build_status
    timing_status = $latestTiming.timing_status
    wns = $latestTiming.wns
    tns = $latestTiming.tns
    whs = $latestTiming.whs
    ths = $latestTiming.ths
    impl_status = $latestTiming.impl_status
    bit_file = $latestTiming.bit_file
  }
  checks = $checks
  artifacts = $artifacts
  safety_boundaries = [ordered]@{
    real_estop_input = 'active_xdc_top_input_local_hard_gate_unverified'
    real_sto_brake_axis_outputs = 'not_connected'
    real_do_pwm_outputs = 'active_xdc_forced_off_local_unverified'
    real_bus_tx_or_driver_enable = 'ps_gem1_emio_rgmii_tx_local_gate_board_evidence_required'
    legacy_axis_adc_boundary = 'retired_external_boundary_only'
    axis_functional_completion = 'vivado_io_owner_connected'
    axis_ena_owner = 'z20_v15_io_owner_axi_lite'
    axis_78_encoder_processing = 'connected_to_step_ip'
    di_mpg_alarm_processing = 'z20_v15_io_owner_input_registers'
    do_pwm_normal_owner = 'z20_v15_io_owner_do_pwm'
    rs485_boundary = 'exported_ps_uart1_emio'
    touch_int_rst_boundary = 'z20_v15_io_owner_tp_int_rst'
    pl_estop_wiring_evidence = 'not_ready'
    board_deploy = 'not_run'
    board_safety_validation = 'not_run'
  }
}

$json = $manifest | ConvertTo-Json -Depth 10
Write-TextFileWithRetry -Path $OutPath -Text ($json + [Environment]::NewLine)

$manifestRel = Convert-ToProjectRelativePath -RootDir $ProjectDir -Path $OutPath
Write-Output "manifest=$manifestRel"
Write-Output "timing_timestamp=$($latestTiming.timestamp)"
Write-Output "build_status=$($latestTiming.build_status)"
Write-Output "timing_status=$($latestTiming.timing_status)"
Write-Output "artifact_count=$($artifacts.Count)"
