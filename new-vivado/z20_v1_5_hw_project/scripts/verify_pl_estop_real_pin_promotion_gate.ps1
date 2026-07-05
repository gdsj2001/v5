param(
  [string]$ProjectDir
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

function Get-ProjectPath {
  param(
    [string]$RootDir,
    [string]$RelativePath
  )
  return Join-Path $RootDir ($RelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
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

  $scriptPath = Get-ProjectPath -RootDir $RootDir -RelativePath $ScriptRelativePath
  if (-not (Test-Path -LiteralPath $scriptPath)) {
    throw "Missing script: $ScriptRelativePath"
  }
  $output = @(& powershell -NoProfile -ExecutionPolicy Bypass -File $scriptPath -ProjectDir $RootDir)
  if ($LASTEXITCODE -ne 0) {
    throw "$ScriptRelativePath failed: $($output -join '; ')"
  }
  return Convert-KeyValueOutput -Lines $output
}

$activeXdcPath = Get-ProjectPath -RootDir $ProjectDir -RelativePath 'constraints/z20_v1_5_active_mapped.xdc'
$wiringCsvPath = Get-ProjectPath -RootDir $ProjectDir -RelativePath 'docs/pl_estop_wiring_evidence.csv'
foreach ($path in @($activeXdcPath, $wiringCsvPath)) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing required promotion-gate input: $path"
  }
}

$activeXdc = Get-Content -LiteralPath $activeXdcPath -Raw -Encoding UTF8
$topPath = Get-ProjectPath -RootDir $ProjectDir -RelativePath 'rtl/system_top.v'
if (-not (Test-Path -LiteralPath $topPath)) {
  throw "Missing required promotion-gate input: $topPath"
}
$top = Get-Content -LiteralPath $topPath -Raw -Encoding UTF8
$wrapperPath = Get-ProjectPath -RootDir $ProjectDir -RelativePath 'z20_v1_5_hw_project.srcs/sources_1/imports/hdl/system_wrapper.v'
$systemSynthPath = Get-ProjectPath -RootDir $ProjectDir -RelativePath 'z20_v1_5_hw_project.gen/sources_1/bd/system/synth/system.v'
foreach ($path in @($wrapperPath, $systemSynthPath)) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing required promotion-gate input: $path"
  }
}
$wrapper = Get-Content -LiteralPath $wrapperPath -Raw -Encoding UTF8
$systemSynth = Get-Content -LiteralPath $systemSynthPath -Raw -Encoding UTF8
$wiringRows = @(Import-Csv -LiteralPath $wiringCsvPath)
$readiness = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_readiness.ps1'

$activeAssignments = New-Object System.Collections.Generic.List[object]
foreach ($line in ($activeXdc -split '\r?\n')) {
  if ($line -match '^\s*set_property\s+PACKAGE_PIN\s+(?<pin>[^\s]+)\s+\[get_ports\s+\{(?<port>[^}]+)\}\]') {
    $activeAssignments.Add([pscustomobject]@{
        pin = $Matches.pin
        port = $Matches.port
      })
  }
}

$safetyGroups = @(
  'estop_input',
  'sto_enable_output',
  'brake_output',
  'axis_gate',
  'general_output',
  'bus_tx_gate'
)

$candidateRows = @($wiringRows | Where-Object {
    $safetyGroups -contains $_.signal_group -and
    -not [string]::IsNullOrWhiteSpace($_.package_pin)
  })

$promoted = New-Object System.Collections.Generic.List[object]
$violations = New-Object System.Collections.Generic.List[string]
foreach ($row in $candidateRows) {
  $matches = @($activeAssignments | Where-Object { $_.pin -eq $row.package_pin })
  foreach ($assignment in $matches) {
    $promoted.Add([pscustomobject]@{
        signal = $row.signal_name
        group = $row.signal_group
        pin = $row.package_pin
        port = $assignment.port
        state = $row.evidence_state
      })

    if ($row.evidence_state -eq 'pending') {
      $violations.Add("$($row.signal_name): active pin $($row.package_pin) is still pending in wiring evidence")
    }
    if ($row.evidence_state -ne 'ready_for_rtl_xdc' -and $row.evidence_state -ne 'board_verified') {
      $violations.Add("$($row.signal_name): active pin $($row.package_pin) has invalid promotion state '$($row.evidence_state)'")
    }
    foreach ($field in @('polarity_or_safe_level', 'normal_owner', 'pl_gate_point', 'schematic_evidence', 'wiring_evidence')) {
      if ([string]::IsNullOrWhiteSpace($row.$field)) {
        $violations.Add("$($row.signal_name): active pin promotion requires $field")
      }
    }
    if ($row.signal_group -eq 'general_output' -and $row.pl_gate_point -notmatch 'pl_estop_general_output_gate|general_output') {
      $violations.Add("$($row.signal_name): DO/PWM promotion must name the PL general-output gate point")
    }
    if ($row.signal_group -eq 'bus_tx_gate' -and $row.pl_gate_point -match 'PHY|RESET|LINK_POWER|LINK_CLK|RX_DISABLE') {
      $violations.Add("$($row.signal_name): bus TX promotion must not target PHY reset, link clock, link power, or RX disable")
    }
  }
}

$activeGateOutputPorts = @(
  [regex]::Matches($activeXdc, 'get_ports\s+\{[^}]*general_output_out[^}]*\}').Count
  [regex]::Matches($activeXdc, 'get_ports\s+\{[^}]*bus_tx_enable_out[^}]*\}').Count
  [regex]::Matches($activeXdc, 'get_ports\s+\{[^}]*bus_tx_queue_flush_req[^}]*\}').Count
  [regex]::Matches($activeXdc, 'get_ports\s+\{[^}]*brake_z_out[^}]*\}').Count
  [regex]::Matches($activeXdc, 'get_ports\s+\{[^}]*enable_out[^}]*\}').Count
  [regex]::Matches($activeXdc, 'get_ports\s+\{[^}]*step_out[^}]*\}').Count
) | Measure-Object -Sum | ForEach-Object { [int]$_.Sum }

$promotedCount = $promoted.Count
$doPwmCount = @($promoted | Where-Object { $_.group -eq 'general_output' }).Count
$estopInputCount = @($promoted | Where-Object { $_.group -eq 'estop_input' }).Count
$busTxCount = @($promoted | Where-Object { $_.group -eq 'bus_tx_gate' }).Count
$requiresE11 = ($promotedCount -gt 0 -or $activeGateOutputPorts -gt 0)
$preOddrBusTxGate = (
  $top -match '\.estop_nc_in\(estop_nc_in\)' -and
  $top -match '\.rgmii_td\(rgmii_td\)' -and
  $top -match '\.rgmii_tx_ctl\(rgmii_tx_ctl\)' -and
  $top -match '\.rgmii_txc\(rgmii_txc\)' -and
  $top -notmatch 'assign\s+rgmii_tx_ctl\s*=|assign\s+rgmii_td\s*=|rgmii_td_from_wrapper|rgmii_tx_ctl_from_wrapper|rgmii_tx_gate_active' -and
  $wrapper -match 'input estop_nc_in' -and
  $wrapper -match '\.estop_nc_in\(estop_nc_in\)' -and
  $systemSynth -match 'input estop_nc_in' -and
  $systemSynth -match 'assign estop_hw_active = ~estop_nc_in' -and
  $systemSynth -match 'pl_estop_bus_tx_gate #\(' -and
  $systemSynth -match 'system_rgmii_tx_ctl_estop_gate' -and
  $systemSynth -match '\.tx_enable_in\(processing_system7_0_GMII_ETHERNET_1_TX_EN\)' -and
  $systemSynth -match '\.tx_enable_out\(gmii_tx_enable_gated\)' -and
  $systemSynth -match 'assign gmii_txd_gated = gmii_tx_gate_active \? 8''h00 : processing_system7_0_GMII_ETHERNET_1_TXD' -and
  $systemSynth -match 'assign gmii_tx_er_gated = gmii_tx_gate_active \? 1''b0 : processing_system7_0_GMII_ETHERNET_1_TX_ER' -and
  $systemSynth -match '\.gmii_tx_en\(gmii_tx_enable_gated\[0\]\)' -and
  $systemSynth -match '\.gmii_tx_er\(gmii_tx_er_gated\)' -and
  $systemSynth -match '\.gmii_txd\(gmii_txd_gated\)' -and
  $systemSynth -match 'assign rgmii_tx_ctl = gmii2rgmii_0_RGMII_TX_CTL' -and
  $systemSynth -match 'assign rgmii_td\[3:0\] = gmii2rgmii_0_RGMII_TD' -and
  $systemSynth -match 'assign rgmii_txc = gmii2rgmii_0_RGMII_TXC'
)
$localHardGatePromoted = (
  $promotedCount -eq 17 -and
  $estopInputCount -eq 1 -and
  $doPwmCount -eq 16 -and
  $busTxCount -eq 0 -and
  $activeGateOutputPorts -eq 0 -and
  $top -match 'input estop_nc_in' -and
  $top -match 'top_do_pwm_estop_gate' -and
  $preOddrBusTxGate -and
  $top -notmatch 'legacy_tmds|\.tmds_tmds_'
)
if ($requiresE11 -and $readiness.e11_rtl_xdc_ready -ne 'yes' -and -not $localHardGatePromoted) {
  $violations.Add('real pin or gate-output promotion requires e11_rtl_xdc_ready=yes')
}

if ($violations.Count -gt 0) {
  Write-Output 'real_pin_promotion_violations:'
  foreach ($violation in $violations) {
    Write-Output "  $violation"
  }
  throw "PL E-stop real-pin promotion gate violations found: $($violations.Count)"
}

$state = 'placeholder_only'
if ($localHardGatePromoted) {
  $state = 'local_hard_gate_promoted'
} elseif ($requiresE11) {
  $state = 'ready_promoted'
}

Write-Output "pl_estop_real_pin_promotion_gate=$state"
Write-Output "active_promoted_wiring_assignments=$promotedCount"
Write-Output "active_promoted_do_pwm_assignments=$doPwmCount"
Write-Output "active_promoted_estop_input_assignments=$estopInputCount"
Write-Output "active_promoted_bus_tx_assignments=$busTxCount"
Write-Output "active_estop_gate_output_ports=$activeGateOutputPorts"
Write-Output "promotion_requires_e11=$(if ($requiresE11 -and -not $localHardGatePromoted) { 'yes' } else { 'no' })"
Write-Output "local_hard_gate_promoted=$(if ($localHardGatePromoted) { 'yes' } else { 'no' })"
Write-Output "e11_rtl_xdc_ready=$($readiness.e11_rtl_xdc_ready)"
Write-Output "a11_board_validation_ready=$($readiness.a11_board_validation_ready)"
