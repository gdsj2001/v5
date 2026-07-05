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

function Read-ProjectText {
  param(
    [string]$RootDir,
    [string]$RelativePath
  )
  $path = Get-ProjectPath -RootDir $RootDir -RelativePath $RelativePath
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing required file: $RelativePath"
  }
  return Get-Content -LiteralPath $path -Raw -Encoding UTF8
}

function Assert-Match {
  param(
    [string]$Text,
    [string]$Pattern,
    [string]$Label
  )
  if ($Text -notmatch $Pattern) {
    throw "Missing PL E-stop safety boundary evidence: $Label"
  }
}

function Assert-NoMatch {
  param(
    [string]$Text,
    [string]$Pattern,
    [string]$Label
  )
  if ($Text -match $Pattern) {
    throw "Forbidden PL E-stop safety boundary evidence found: $Label"
  }
}

$core = Read-ProjectText -RootDir $ProjectDir -RelativePath 'rtl/pl_estop_core.v'
$axi = Read-ProjectText -RootDir $ProjectDir -RelativePath 'rtl/pl_estop_axi_lite.v'
$bdScript = Read-ProjectText -RootDir $ProjectDir -RelativePath 'scripts/vivado/add_pl_estop_axi_lite.tcl'
$gmiiPatchScript = Read-ProjectText -RootDir $ProjectDir -RelativePath 'scripts/patch_gmii_pre_oddr_estop_gate.ps1'
$synthTcl = Read-ProjectText -RootDir $ProjectDir -RelativePath 'scripts/vivado_synth_current.tcl'
$implTcl = Read-ProjectText -RootDir $ProjectDir -RelativePath 'scripts/vivado_impl_current.tcl'
$bd = Read-ProjectText -RootDir $ProjectDir -RelativePath 'z20_v1_5_hw_project.srcs/sources_1/bd/system/system.bd'
$activeXdc = Read-ProjectText -RootDir $ProjectDir -RelativePath 'constraints/z20_v1_5_active_mapped.xdc'
$top = Read-ProjectText -RootDir $ProjectDir -RelativePath 'rtl/system_top.v'
$wrapper = Read-ProjectText -RootDir $ProjectDir -RelativePath 'z20_v1_5_hw_project.srcs/sources_1/imports/hdl/system_wrapper.v'
$systemSynth = Read-ProjectText -RootDir $ProjectDir -RelativePath 'z20_v1_5_hw_project.gen/sources_1/bd/system/synth/system.v'
$integrationDoc = Read-ProjectText -RootDir $ProjectDir -RelativePath 'docs/pl_estop_integration.md'
$sourceIntentDocDir = (Resolve-Path -LiteralPath (Get-ProjectPath -RootDir $ProjectDir -RelativePath '..')).Path
$sourceIntentDocPath = @(Get-ChildItem -LiteralPath $sourceIntentDocDir -File -Filter 'pl*.md' | Select-Object -First 1)
if ($sourceIntentDocPath.Count -eq 0) {
  throw 'Missing source PL E-stop doc matching ../pl*.md'
}
$sourceIntentDoc = Get-Content -LiteralPath $sourceIntentDocPath[0].FullName -Raw -Encoding UTF8
$readme = Read-ProjectText -RootDir $ProjectDir -RelativePath 'README.md'
$wiringEvidencePath = Get-ProjectPath -RootDir $ProjectDir -RelativePath 'docs/pl_estop_wiring_evidence.csv'
if (-not (Test-Path -LiteralPath $wiringEvidencePath)) {
  throw 'Missing docs/pl_estop_wiring_evidence.csv'
}
$wiringEvidenceRows = @(Import-Csv -LiteralPath $wiringEvidencePath)

Assert-Match -Text $core -Pattern 'parameter integer GENERAL_OUTPUT_COUNT = 16' -Label 'GENERAL_OUTPUT_COUNT is 16'
Assert-Match -Text $core -Pattern 'parameter \[GENERAL_OUTPUT_COUNT-1:0\] GENERAL_OUTPUT_SAFE_LEVELS = \{GENERAL_OUTPUT_COUNT\{1''b0\}\}' -Label 'GENERAL_OUTPUT_SAFE_LEVELS default zero'
Assert-Match -Text $core -Pattern 'module\s+pl_estop_general_output_gate' -Label 'general output gate module'
Assert-Match -Text $core -Pattern 'output_out\[output_idx\] = estop_latched \? SAFE_LEVELS\[output_idx\] : output_in\[output_idx\]' -Label 'general output forced-off mux'
Assert-Match -Text $core -Pattern 'assign forced_off = estop_latched' -Label 'general output forced_off status'
Assert-Match -Text $core -Pattern 'parameter integer BUS_TX_GATE_COUNT = 1' -Label 'BUS_TX_GATE_COUNT is 1'
Assert-Match -Text $core -Pattern 'parameter \[BUS_TX_GATE_COUNT-1:0\] BUS_TX_IDLE_LEVELS = \{BUS_TX_GATE_COUNT\{1''b0\}\}' -Label 'BUS_TX_IDLE_LEVELS default zero'
Assert-Match -Text $core -Pattern 'module\s+pl_estop_bus_tx_gate' -Label 'bus TX gate module'
Assert-Match -Text $core -Pattern 'tx_enable_out\[gate_idx\] = estop_latched \? IDLE_LEVELS\[gate_idx\] : tx_enable_in\[gate_idx\]' -Label 'bus TX idle mux'
Assert-Match -Text $core -Pattern 'assign queue_flush_req = estop_latched' -Label 'bus TX flush request on latch'
Assert-Match -Text $core -Pattern 'reset_allowed = estop_input_raw && estop_input_filtered && !brake_delay_active && bus_tx_queue_flushed' -Label 'reset waits for bus TX queue flush'

Assert-Match -Text $axi -Pattern 'module\s+pl_estop_axi_lite_v3' -Label 'AXI v3 wrapper'
Assert-Match -Text $axi -Pattern 'ADDR_GENERAL_CONFIG' -Label 'general output config register'
Assert-Match -Text $axi -Pattern 'ADDR_BUS_TX_CONFIG' -Label 'bus TX config register'
Assert-Match -Text $axi -Pattern '(?s)read_reg = \{\s*23''d0,\s*bus_tx_queue_flushed,\s*bus_tx_gate_active,\s*general_output_forced_off,' -Label 'STATUS bits 8:6 map queue/gate/general'
Assert-Match -Text $axi -Pattern '\.GENERAL_OUTPUT_COUNT\(16\)' -Label 'AXI v3 exposes 16 general outputs'
Assert-Match -Text $axi -Pattern '\.BUS_TX_GATE_COUNT\(1\)' -Label 'AXI v3 exposes one bus TX gate'

Assert-Match -Text $bdScript -Pattern 'set estop_ref pl_estop_axi_lite_v3' -Label 'BD uses v3 module reference'
Assert-Match -Text $bdScript -Pattern 'connect_pin_if_open cnc_const_zero/dout \$estop_cell/estop_nc_in' -Label 'BD NC input is fail-closed placeholder'
Assert-Match -Text $bdScript -Pattern 'CONFIG.CONST_WIDTH \{16\} CONFIG.CONST_VAL \{0\}' -Label 'BD general output placeholder is 16-bit zero'
Assert-Match -Text $bdScript -Pattern 'connect_pin_if_open \$general_zero_cell/dout \$estop_cell/general_output_in' -Label 'BD connects DO/PWM placeholder into gate'
Assert-Match -Text $bdScript -Pattern 'connect_pin_if_open \$bus_tx_zero_cell/dout \$estop_cell/bus_tx_enable_in' -Label 'BD connects bus TX placeholder into gate'
Assert-Match -Text $bdScript -Pattern 'CONFIG.CONST_WIDTH \{1\} CONFIG.CONST_VAL \{1\}' -Label 'BD bus TX queue-flushed placeholder is true'
Assert-Match -Text $bdScript -Pattern 'connect_pin_if_open \$bus_tx_flushed_cell/dout \$estop_cell/bus_tx_queue_flushed_in' -Label 'BD connects bus TX queue-flushed placeholder'
Assert-NoMatch -Text $bdScript -Pattern '\$estop_cell/(step_out|enable_out|brake_z_out|general_output_out|bus_tx_enable_out|bus_tx_queue_flush_req)' -Label 'BD script must not connect real E-stop outputs before wiring evidence'

Assert-Match -Text $gmiiPatchScript -Pattern 'Patch-SystemWrapper' -Label 'GMII pre-ODDR patch covers generated wrapper'
Assert-Match -Text $gmiiPatchScript -Pattern 'Patch-SystemSynth' -Label 'GMII pre-ODDR patch covers BD synth netlist'
Assert-Match -Text $gmiiPatchScript -Pattern 'system_rgmii_tx_ctl_estop_gate' -Label 'GMII pre-ODDR patch restores gate instance'
Assert-Match -Text $gmiiPatchScript -Pattern 'gmii_tx_enable_gated' -Label 'GMII pre-ODDR patch restores gated TX_EN'
Assert-Match -Text $gmiiPatchScript -Pattern 'gmii_txd_gated' -Label 'GMII pre-ODDR patch restores gated TXD'
Assert-Match -Text $gmiiPatchScript -Pattern 'gmii_tx_er_gated' -Label 'GMII pre-ODDR patch restores gated TX_ER'
Assert-Match -Text $gmiiPatchScript -Pattern 'pl_estop_axi_observation_patch=top_estop_input' -Label 'generated patch aligns AXI status observation with top E-stop input'
Assert-Match -Text $gmiiPatchScript -Pattern 'CheckOnly' -Label 'GMII pre-ODDR patch supports check-only verification'
Assert-Match -Text $synthTcl -Pattern 'patch_gmii_pre_oddr_estop_gate\.ps1' -Label 'synthesis flow applies GMII pre-ODDR patch'
Assert-Match -Text $implTcl -Pattern 'patch_gmii_pre_oddr_estop_gate\.ps1' -Label 'implementation flow applies GMII pre-ODDR patch'

Assert-Match -Text $bd -Pattern 'pl_estop_axi_lite_v3' -Label 'saved BD uses v3 module'
Assert-Match -Text $bd -Pattern 'pl_estop_do_zero/dout' -Label 'saved BD has DO/PWM zero placeholder'
Assert-Match -Text $bd -Pattern 'pl_estop/general_output_in' -Label 'saved BD connects DO/PWM placeholder input'
Assert-Match -Text $bd -Pattern 'pl_estop_tx_zero/dout' -Label 'saved BD has bus TX zero placeholder'
Assert-Match -Text $bd -Pattern 'pl_estop/bus_tx_enable_in' -Label 'saved BD connects bus TX placeholder input'
Assert-Match -Text $bd -Pattern 'pl_estop_tx_flushed/dout' -Label 'saved BD has bus TX queue-flushed placeholder'
Assert-Match -Text $bd -Pattern 'pl_estop/bus_tx_queue_flushed_in' -Label 'saved BD connects bus TX queue-flushed placeholder'

Assert-Match -Text $top -Pattern 'input estop_nc_in' -Label 'top exposes physical E-stop input'
Assert-Match -Text $top -Pattern 'output \[13:0\]do_out' -Label 'top exposes 14 DO outputs'
Assert-Match -Text $top -Pattern 'output \[1:0\]pwm_out' -Label 'top exposes 2 PWM outputs'
Assert-Match -Text $top -Pattern 'assign estop_hw_active = ~estop_nc_in' -Label 'top derives hard E-stop from NC input'
Assert-Match -Text $top -Pattern 'top_do_pwm_estop_gate' -Label 'top instantiates DO/PWM hard gate'
Assert-Match -Text $top -Pattern 'pl_estop_general_output_gate #\(' -Label 'top uses shared general-output gate module'
Assert-Match -Text $top -Pattern '\.output_out\(do_pwm_gated\)' -Label 'top routes DO/PWM through gated vector'
Assert-Match -Text $top -Pattern 'assign do_out = do_pwm_gated\[13:0\]' -Label 'top drives DO outputs from gated vector'
Assert-Match -Text $top -Pattern 'assign pwm_out = do_pwm_gated\[15:14\]' -Label 'top drives PWM outputs from gated vector'
Assert-Match -Text $top -Pattern '\.estop_nc_in\(estop_nc_in\)' -Label 'top passes physical E-stop into wrapper'
Assert-Match -Text $top -Pattern '\.rgmii_td\(rgmii_td\)' -Label 'top keeps RGMII TD direct from wrapper'
Assert-Match -Text $top -Pattern '\.rgmii_tx_ctl\(rgmii_tx_ctl\)' -Label 'top keeps RGMII TX_CTL direct from wrapper'
Assert-Match -Text $top -Pattern '\.rgmii_txc\(rgmii_txc\)' -Label 'top preserves RGMII TX clock path'
Assert-NoMatch -Text $top -Pattern 'assign\s+rgmii_tx_ctl\s*=|assign\s+rgmii_td\s*=|rgmii_td_from_wrapper|rgmii_tx_ctl_from_wrapper|rgmii_tx_gate_active' -Label 'top must not gate post-ODDR RGMII outputs'
Assert-NoMatch -Text $top -Pattern 'legacy_tmds|\.tmds_tmds_' -Label 'top must not connect retired HDMI wrapper ports'

Assert-Match -Text $wrapper -Pattern 'input estop_nc_in' -Label 'wrapper exposes physical E-stop input'
Assert-Match -Text $wrapper -Pattern '\.estop_nc_in\(estop_nc_in\)' -Label 'wrapper passes physical E-stop into BD synth module'
Assert-Match -Text $wrapper -Pattern '\.rgmii_td\(rgmii_td\)' -Label 'wrapper preserves RGMII TD output'
Assert-Match -Text $wrapper -Pattern '\.rgmii_tx_ctl\(rgmii_tx_ctl\)' -Label 'wrapper preserves RGMII TX_CTL output'
Assert-Match -Text $wrapper -Pattern '\.rgmii_txc\(rgmii_txc\)' -Label 'wrapper preserves RGMII TXC output'

Assert-Match -Text $systemSynth -Pattern 'input estop_nc_in' -Label 'BD synth module exposes physical E-stop input'
Assert-Match -Text $systemSynth -Pattern 'assign estop_hw_active = ~estop_nc_in' -Label 'BD synth module derives hard E-stop'
Assert-Match -Text $systemSynth -Pattern 'pl_estop_bus_tx_gate #\(' -Label 'BD synth module instantiates bus TX gate'
Assert-Match -Text $systemSynth -Pattern 'system_rgmii_tx_ctl_estop_gate' -Label 'BD synth module names the RGMII TX gate'
Assert-Match -Text $systemSynth -Pattern '\.tx_enable_in\(processing_system7_0_GMII_ETHERNET_1_TX_EN\)' -Label 'BD synth module gates GMII TX_EN before gmii2rgmii'
Assert-Match -Text $systemSynth -Pattern '\.tx_enable_out\(gmii_tx_enable_gated\)' -Label 'BD synth module emits gated GMII TX_EN'
Assert-Match -Text $systemSynth -Pattern 'assign gmii_txd_gated = gmii_tx_gate_active \? 8''h00 : processing_system7_0_GMII_ETHERNET_1_TXD' -Label 'BD synth module zeros GMII TXD while gate active'
Assert-Match -Text $systemSynth -Pattern 'assign gmii_tx_er_gated = gmii_tx_gate_active \? 1''b0 : processing_system7_0_GMII_ETHERNET_1_TX_ER' -Label 'BD synth module clears GMII TX_ER while gate active'
Assert-Match -Text $systemSynth -Pattern '\.gmii_tx_en\(gmii_tx_enable_gated\[0\]\)' -Label 'gmii2rgmii consumes gated GMII TX_EN'
Assert-Match -Text $systemSynth -Pattern '\.gmii_tx_er\(gmii_tx_er_gated\)' -Label 'gmii2rgmii consumes gated GMII TX_ER'
Assert-Match -Text $systemSynth -Pattern '\.gmii_txd\(gmii_txd_gated\)' -Label 'gmii2rgmii consumes gated GMII TXD'
Assert-Match -Text $systemSynth -Pattern '\.estop_nc_in\(estop_nc_in(_2)?\),' -Label 'BD synth pl_estop status observes top physical E-stop input'
Assert-NoMatch -Text $systemSynth -Pattern '\.estop_nc_in\(cnc_const_zero_dout\),' -Label 'BD synth pl_estop status must not remain tied to fail-closed constant'
Assert-Match -Text $systemSynth -Pattern 'assign rgmii_tx_ctl = gmii2rgmii_0_RGMII_TX_CTL' -Label 'BD synth module keeps RGMII TX_CTL direct after gmii2rgmii'
Assert-Match -Text $systemSynth -Pattern 'assign rgmii_td\[3:0\] = gmii2rgmii_0_RGMII_TD' -Label 'BD synth module keeps RGMII TD direct after gmii2rgmii'
Assert-Match -Text $systemSynth -Pattern 'assign rgmii_txc = gmii2rgmii_0_RGMII_TXC' -Label 'BD synth module keeps RGMII TXC direct after gmii2rgmii'

$doPwmPins = @('A21','A22','B21','B22','C22','D22','F21','F22','F19','G19','F17','G17','G21','G20','G22','H22')
$activeDoPwmPinAssignments = 0
foreach ($pin in $doPwmPins) {
  if ($activeXdc -match "(?m)^\s*set_property\s+PACKAGE_PIN\s+$pin\s+\[get_ports") {
    $activeDoPwmPinAssignments += 1
  }
}

$activePackagePinAssignments = New-Object System.Collections.Generic.List[object]
foreach ($line in ($activeXdc -split '\r?\n')) {
  if ($line -match '^\s*set_property\s+PACKAGE_PIN\s+(?<pin>[^\s]+)\s+\[get_ports\s+\{(?<port>[^}]+)\}\]') {
    $activePackagePinAssignments.Add([pscustomobject]@{
        pin = $Matches.pin
        port = $Matches.port
      })
  }
}

$safetyEvidenceGroups = @(
  'estop_input',
  'sto_enable_output',
  'brake_output',
  'axis_gate',
  'general_output',
  'bus_tx_gate'
)
$pendingWiringRowsWithPins = @($wiringEvidenceRows | Where-Object {
    $_.evidence_state -eq 'pending' -and
    $safetyEvidenceGroups -contains $_.signal_group -and
    -not [string]::IsNullOrWhiteSpace($_.package_pin)
  })
$pendingWiringPinAssignments = New-Object System.Collections.Generic.List[string]
foreach ($row in $pendingWiringRowsWithPins) {
  foreach ($assignment in $activePackagePinAssignments) {
    if ($assignment.pin -eq $row.package_pin) {
      $pendingWiringPinAssignments.Add("$($row.signal_name):$($row.package_pin):$($assignment.port)")
    }
  }
}
$activePendingWiringPinAssignments = $pendingWiringPinAssignments.Count
$activeEstopInputPinAssignments = @($activePackagePinAssignments | Where-Object { $_.pin -eq 'AA19' }).Count

$activeEstopGateOutputPorts = @(
  [regex]::Matches($activeXdc, 'get_ports\s+\{[^}]*general_output_out[^}]*\}').Count
  [regex]::Matches($activeXdc, 'get_ports\s+\{[^}]*bus_tx_enable_out[^}]*\}').Count
  [regex]::Matches($activeXdc, 'get_ports\s+\{[^}]*bus_tx_queue_flush_req[^}]*\}').Count
) | Measure-Object -Sum | ForEach-Object { [int]$_.Sum }

if ($activeDoPwmPinAssignments -ne 16) {
  throw "Active XDC must assign exactly 16 top-level hard-gated DO/PWM package pins, found: $activeDoPwmPinAssignments"
}
if ($activeEstopInputPinAssignments -ne 1) {
  throw "Active XDC must assign exactly one physical E-stop input package pin, found: $activeEstopInputPinAssignments"
}
if ($activePendingWiringPinAssignments -ne 0) {
  throw "Active XDC assigns pending PL E-stop wiring-evidence pins: $($pendingWiringPinAssignments -join ', ')"
}
if ($activeEstopGateOutputPorts -ne 0) {
  throw "Active XDC exposes PL E-stop gate outputs before wiring evidence: $activeEstopGateOutputPorts"
}

Assert-Match -Text $integrationDoc -Pattern 'DO1` - `DO14` and `PWM1` - `PWM2`' -Label 'integration doc names DO/PWM shutdown range'
Assert-Match -Text $integrationDoc -Pattern 'must be shut off by the PL E-stop path' -Label 'integration doc requires PL shutdown for DO/PWM'
Assert-Match -Text $integrationDoc -Pattern 'not to break the physical link' -Label 'integration doc keeps bus physical link alive'
Assert-Match -Text $integrationDoc -Pattern 'flush or invalidate any queued TX FIFO entry' -Label 'integration doc requires queued TX flush or invalidate'
Assert-Match -Text $sourceIntentDoc -Pattern 'DO1.*DO14.*PWM1.*PWM2' -Label 'source PL E-stop doc names DO/PWM shutdown range'
Assert-Match -Text $sourceIntentDoc -Pattern 'off/safe' -Label 'source PL E-stop doc requires DO/PWM off or safe level'
Assert-Match -Text $sourceIntentDoc -Pattern 'PS.*LinuxCNC.*UI' -Label 'source PL E-stop doc keeps DO/PWM independent of software'
Assert-Match -Text $sourceIntentDoc -Pattern 'Link' -Label 'source PL E-stop doc keeps bus physical Link alive'
Assert-Match -Text $sourceIntentDoc -Pattern 'TX FIFO' -Label 'source PL E-stop doc requires stale TX flush or invalidate'
Assert-Match -Text $readme -Pattern 'PL E-stop DO/PWM rule' -Label 'README records DO/PWM rule'
Assert-Match -Text $readme -Pattern 'PL E-stop bus TX rule' -Label 'README records bus TX rule'

Write-Output 'pl_estop_safety_boundary=ok'
Write-Output 'do_pwm_gate=top_hard_gate_local_unverified'
Write-Output 'bus_tx_gate=top_rgmii_tx_gate_local_unverified'
Write-Output 'pl_estop_axi_observation=top_estop_input_local_unverified'
Write-Output 'gmii_pre_oddr_patch_flow=ok'
Write-Output 'status_bits=ok'
Write-Output 'bd_placeholder_connections=ok'
Write-Output "active_do_pwm_pin_assignments=$activeDoPwmPinAssignments"
Write-Output "active_estop_input_pin_assignments=$activeEstopInputPinAssignments"
Write-Output "active_pending_wiring_pin_assignments=$activePendingWiringPinAssignments"
Write-Output "active_estop_gate_output_ports=$activeEstopGateOutputPorts"
