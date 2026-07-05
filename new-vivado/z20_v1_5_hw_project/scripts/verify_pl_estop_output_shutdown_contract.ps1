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
    throw "Missing PL E-stop output-shutdown contract evidence: $Label"
  }
}

function Assert-NoActivePendingPin {
  param(
    [object[]]$Assignments,
    [object[]]$Rows,
    [string]$Group,
    [string]$Label
  )

  foreach ($row in @($Rows | Where-Object { $_.signal_group -eq $Group -and -not [string]::IsNullOrWhiteSpace($_.package_pin) })) {
    foreach ($assignment in @($Assignments | Where-Object { $_.pin -eq $row.package_pin })) {
      if ($row.evidence_state -eq 'pending') {
        throw "$Label active assignment is still pending in wiring evidence: $($row.signal_name):$($row.package_pin):$($assignment.port)"
      }
    }
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

$sourceDocDir = (Resolve-Path -LiteralPath (Get-ProjectPath -RootDir $ProjectDir -RelativePath '..')).Path
$sourceDocPath = @(Get-ChildItem -LiteralPath $sourceDocDir -File -Filter 'pl*.md' | Select-Object -First 1)
if ($sourceDocPath.Count -eq 0) {
  throw 'Missing source PL E-stop doc matching ../pl*.md'
}
$planPath = @(Get-ChildItem -LiteralPath $sourceDocDir -File -Filter '*vivado*.md' | Select-Object -First 1)
if ($planPath.Count -eq 0) {
  throw 'Missing new Vivado plan doc matching ../*vivado*.md'
}

$sourceDoc = Get-Content -LiteralPath $sourceDocPath[0].FullName -Raw -Encoding UTF8
$plan = Get-Content -LiteralPath $planPath[0].FullName -Raw -Encoding UTF8
$readme = Read-ProjectText -RootDir $ProjectDir -RelativePath 'README.md'
$integrationDoc = Read-ProjectText -RootDir $ProjectDir -RelativePath 'docs/pl_estop_integration.md'
$runbook = Read-ProjectText -RootDir $ProjectDir -RelativePath 'docs/pl_estop_field_execution_runbook.md'
$core = Read-ProjectText -RootDir $ProjectDir -RelativePath 'rtl/pl_estop_core.v'
$axi = Read-ProjectText -RootDir $ProjectDir -RelativePath 'rtl/pl_estop_axi_lite.v'
$coreTb = Read-ProjectText -RootDir $ProjectDir -RelativePath 'sim/pl_estop_core_tb.v'
$activeXdc = Read-ProjectText -RootDir $ProjectDir -RelativePath 'constraints/z20_v1_5_active_mapped.xdc'

$wiringPath = Get-ProjectPath -RootDir $ProjectDir -RelativePath 'docs/pl_estop_wiring_evidence.csv'
$boardPath = Get-ProjectPath -RootDir $ProjectDir -RelativePath 'docs/pl_estop_board_validation_evidence.csv'
if (-not (Test-Path -LiteralPath $wiringPath)) {
  throw 'Missing docs/pl_estop_wiring_evidence.csv'
}
if (-not (Test-Path -LiteralPath $boardPath)) {
  throw 'Missing docs/pl_estop_board_validation_evidence.csv'
}
$wiringRows = @(Import-Csv -LiteralPath $wiringPath)
$boardRows = @(Import-Csv -LiteralPath $boardPath)
$busGateOwner = Invoke-ProjectScript -RootDir $ProjectDir -ScriptRelativePath 'scripts/verify_pl_estop_bus_gate_owner.ps1'

$requiredDoPwmSignals = @(
  'DO1','DO2','DO3','DO4','DO5','DO6','DO7','DO8',
  'DO9','DO10','DO11','DO12','DO13','DO14','PWM1','PWM2'
)
$doPwmRows = @($wiringRows | Where-Object { $_.signal_group -eq 'general_output' })
foreach ($signal in $requiredDoPwmSignals) {
  $matches = @($doPwmRows | Where-Object { $_.signal_name -eq $signal })
  if ($matches.Count -ne 1) {
    throw "Expected exactly one DO/PWM wiring row for $signal, found $($matches.Count)"
  }
  if ($matches[0].next_action -notmatch 'pl_estop_general_output_gate') {
    throw "$signal next_action must name pl_estop_general_output_gate"
  }
}

$busTxRows = @($wiringRows | Where-Object { $_.signal_group -eq 'bus_tx_gate' })
foreach ($signal in @('bus_tx_driver_enable', 'bus_tx_queue_flush')) {
  $matches = @($busTxRows | Where-Object { $_.signal_name -eq $signal })
  if ($matches.Count -ne 1) {
    throw "Expected exactly one bus TX wiring row for $signal, found $($matches.Count)"
  }
}

foreach ($testId in @('BV09', 'BV10', 'BV11', 'BV12')) {
  $matches = @($boardRows | Where-Object { $_.test_id -eq $testId })
  if ($matches.Count -ne 1) {
    throw "Expected exactly one board-validation row for $testId, found $($matches.Count)"
  }
}

Assert-Match -Text $sourceDoc -Pattern 'DO1.*DO14.*PWM1.*PWM2' -Label 'source doc DO/PWM range'
Assert-Match -Text $sourceDoc -Pattern 'off/safe' -Label 'source doc DO/PWM off/safe level'
Assert-Match -Text $sourceDoc -Pattern 'PS.*LinuxCNC.*UI' -Label 'source doc software-independent output shutdown'
Assert-Match -Text $sourceDoc -Pattern 'Link' -Label 'source doc bus TX link preservation'
Assert-Match -Text $sourceDoc -Pattern 'TX FIFO' -Label 'source doc stale TX flush or invalidate'

foreach ($doc in @($plan, $readme, $integrationDoc, $runbook)) {
  Assert-Match -Text $doc -Pattern 'DO1.*DO14.*PWM1.*PWM2' -Label 'derived doc DO/PWM range'
  Assert-Match -Text $doc -Pattern 'pl_estop_general_output_gate' -Label 'derived doc general output gate'
  Assert-Match -Text $doc -Pattern 'pl_estop_bus_tx_gate' -Label 'derived doc bus TX gate'
  Assert-Match -Text $doc -Pattern 'Link' -Label 'derived doc link preservation'
}

Assert-Match -Text $core -Pattern 'parameter integer GENERAL_OUTPUT_COUNT = 16' -Label 'RTL has 16 general outputs'
Assert-Match -Text $core -Pattern 'module\s+pl_estop_general_output_gate' -Label 'RTL general output gate module'
Assert-Match -Text $core -Pattern 'output_out\[output_idx\] = estop_latched \? SAFE_LEVELS\[output_idx\] : output_in\[output_idx\]' -Label 'RTL general output forced-off mux'
Assert-Match -Text $core -Pattern 'module\s+pl_estop_bus_tx_gate' -Label 'RTL bus TX gate module'
Assert-Match -Text $core -Pattern 'tx_enable_out\[gate_idx\] = estop_latched \? IDLE_LEVELS\[gate_idx\] : tx_enable_in\[gate_idx\]' -Label 'RTL bus TX idle mux'
Assert-Match -Text $core -Pattern 'assign queue_flush_req = estop_latched' -Label 'RTL bus TX queue flush request'
Assert-Match -Text $axi -Pattern 'ADDR_GENERAL_CONFIG' -Label 'AXI general config register'
Assert-Match -Text $axi -Pattern 'ADDR_BUS_TX_CONFIG' -Label 'AXI bus TX config register'
Assert-Match -Text $axi -Pattern 'general_output_forced_off' -Label 'AXI general output status bit'
Assert-Match -Text $axi -Pattern 'bus_tx_gate_active' -Label 'AXI bus TX gate status bit'
Assert-Match -Text $axi -Pattern 'bus_tx_queue_flushed' -Label 'AXI bus TX queue status bit'
Assert-Match -Text $coreTb -Pattern 'general outputs forced off after reset' -Label 'simulation reset forced-off coverage'
Assert-Match -Text $coreTb -Pattern 'general outputs pass when not latched' -Label 'simulation general output pass coverage'
Assert-Match -Text $coreTb -Pattern 'latched estop must force general outputs off' -Label 'simulation E-stop forced-off coverage'
Assert-Match -Text $coreTb -Pattern 'latched estop must force bus TX enable idle' -Label 'simulation bus TX idle coverage'
Assert-Match -Text $coreTb -Pattern 'reset must wait for bus TX queue flush' -Label 'simulation queue flush reset interlock'

$activeAssignments = New-Object System.Collections.Generic.List[object]
foreach ($line in ($activeXdc -split '\r?\n')) {
  if ($line -match '^\s*set_property\s+PACKAGE_PIN\s+(?<pin>[^\s]+)\s+\[get_ports\s+\{(?<port>[^}]+)\}\]') {
    $activeAssignments.Add([pscustomobject]@{
        pin = $Matches.pin
        port = $Matches.port
      })
  }
}

$activeAssignmentRows = @($activeAssignments.ToArray())
Assert-NoActivePendingPin -Assignments $activeAssignmentRows -Rows $wiringRows -Group 'general_output' -Label 'DO/PWM'
Assert-NoActivePendingPin -Assignments $activeAssignmentRows -Rows $wiringRows -Group 'bus_tx_gate' -Label 'bus TX'

$activeDoPwmPinAssignments = 0
foreach ($row in $doPwmRows) {
  if (-not [string]::IsNullOrWhiteSpace($row.package_pin)) {
    $activeDoPwmPinAssignments += @($activeAssignments | Where-Object { $_.pin -eq $row.package_pin }).Count
  }
}

$activeBusTxPinAssignments = 0
foreach ($row in $busTxRows) {
  if (-not [string]::IsNullOrWhiteSpace($row.package_pin)) {
    $activeBusTxPinAssignments += @($activeAssignments | Where-Object { $_.pin -eq $row.package_pin }).Count
  }
}

$activeOutputGatePorts = @(
  [regex]::Matches($activeXdc, 'get_ports\s+\{[^}]*general_output_out[^}]*\}').Count
  [regex]::Matches($activeXdc, 'get_ports\s+\{[^}]*bus_tx_enable_out[^}]*\}').Count
  [regex]::Matches($activeXdc, 'get_ports\s+\{[^}]*bus_tx_queue_flush_req[^}]*\}').Count
) | Measure-Object -Sum | ForEach-Object { [int]$_.Sum }

$doPwmPendingRows = @($doPwmRows | Where-Object { $_.evidence_state -eq 'pending' }).Count
$busTxPendingRows = @($busTxRows | Where-Object { $_.evidence_state -eq 'pending' }).Count
$bv09State = (@($boardRows | Where-Object { $_.test_id -eq 'BV09' })[0]).evidence_state
$bv10State = (@($boardRows | Where-Object { $_.test_id -eq 'BV10' })[0]).evidence_state
$bv11State = (@($boardRows | Where-Object { $_.test_id -eq 'BV11' })[0]).evidence_state
$bv12State = (@($boardRows | Where-Object { $_.test_id -eq 'BV12' })[0]).evidence_state

$contractState = 'code_review_only'

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
    $busGateOwner.link_reset_power_gate -ne 'not_used' -or
    $busGateOwner.board_tests_required -ne 'BV10,BV11,BV12' -or
    $busGateOwner.board_evidence_state -ne 'pending') {
  throw 'PL E-stop bus gate owner check failed'
}

Write-Output "pl_estop_output_shutdown_contract=$contractState"
Write-Output 'do_pwm_contract=ok'
Write-Output 'bus_tx_contract=ok'
Write-Output "bus_gate_owner=$($busGateOwner.pl_estop_bus_gate_owner)"
Write-Output "bus_gate_transport=$($busGateOwner.production_transport)"
Write-Output "bus_gate_before_gmii2rgmii=$($busGateOwner.gate_inserted_before_gmii2rgmii)"
Write-Output "bus_gate_board_evidence=$($busGateOwner.board_evidence_state)"
Write-Output "do_pwm_wiring_rows=$($doPwmRows.Count)"
Write-Output "do_pwm_pending_rows=$doPwmPendingRows"
Write-Output "bus_tx_wiring_rows=$($busTxRows.Count)"
Write-Output "bus_tx_pending_rows=$busTxPendingRows"
Write-Output "board_test_bv09=$bv09State"
Write-Output "board_test_bv10=$bv10State"
Write-Output "board_test_bv11=$bv11State"
Write-Output "board_test_bv12=$bv12State"
Write-Output "active_do_pwm_pin_assignments=$activeDoPwmPinAssignments"
Write-Output "active_bus_tx_pin_assignments=$activeBusTxPinAssignments"
Write-Output "active_output_gate_ports=$activeOutputGatePorts"
